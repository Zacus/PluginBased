# 运行时 QML import path 收敛设计

## 背景

当前主程序在 `app/main.cpp` 中通过编译期宏 `QML_IMPORT_PATH="${CMAKE_BINARY_DIR}"` 向 `QQmlApplicationEngine` 添加 QML 模块搜索路径。这个设计对开发构建有效，因为构建目录下同时存在 `PluginBased/`、`PlayPlugin/`、`AppLog/`、`QuickUI/` 等 QML 模块目录。

发布包不应该依赖构建目录。项目的打包配置已经约定了运行时 QML 模块位置：

- macOS bundle：`PluginBasedApp.app/Contents/Resources/qml`
- Linux：`<appDir>/qml`
- Windows：`<appDir>/qml`

但主程序目前没有显式把这些运行时目录加入 `QQmlApplicationEngine`。这会让发布包依赖打包工具、环境变量或构建目录残留，影响企业交付的可复现性。

## 目标

1. 主程序启动时显式添加运行时 QML import path。
2. 开发构建继续支持从构建目录加载 QML 模块。
3. 发布包运行时不依赖 `CMAKE_BINARY_DIR`。
4. 路径探测逻辑清晰、可日志诊断、跨 macOS/Linux/Windows 一致。
5. 与现有插件加载顺序兼容：插件动态库仍然在 `QQmlApplicationEngine` 创建前加载。
6. 增加轻量测试或结构检查，防止后续退化为只依赖编译期路径。

## 非目标

1. 不改变插件 ABI、metadata schema 或插件 manifest。
2. 不改变 QML 模块打包目录结构。
3. 不重构 `tools/deploy.py` 的发布包布局。
4. 不引入新的配置项让用户自定义 QML import path。
5. 不支持从任意用户目录加载 QML 模块。

## 当前问题

`main.cpp` 当前只在宏存在时添加：

```cpp
#ifdef QML_IMPORT_PATH
    engine.addImportPath(QStringLiteral(QML_IMPORT_PATH));
#endif
```

这有两个问题：

- `QML_IMPORT_PATH` 是构建期路径，发布包中不稳定。
- 运行时布局已经由 `tools/package.yml` 定义，但应用启动代码没有对应逻辑。

此外，README 和打包配置都描述了发布包 QML 目录，但主程序没有把这些约定固化成代码，文档和实现之间存在隐性差异。

## 方案比较

### 方案 A：只使用运行时路径

启动时只添加发布包约定路径，例如 `appDir + "/qml"` 和 macOS bundle 的 `Contents/Resources/qml`。

优点：

- 发布逻辑最干净。
- 不再暴露构建目录。

缺点：

- 开发构建中 QML 模块仍在 `CMAKE_BINARY_DIR`，需要额外复制模块到 `build/app/qml` 或改 CMake 输出布局。
- 改动范围扩大。

### 方案 B：运行时路径优先，开发路径兜底

启动时先添加运行时 QML 目录；如果是开发构建，再添加 `QML_IMPORT_PATH` 作为兜底。

优点：

- 发布包路径成为主路径。
- 保持当前开发体验。
- 改动集中在 `main.cpp` 和测试。

缺点：

- 开发构建仍然会保留 `QML_IMPORT_PATH` 宏。
- 需要通过日志和测试确保发布路径存在时优先使用。

### 方案 C：统一 CMake 输出到运行时布局

调整所有 QML 模块输出目录，使开发构建和发布包都使用 `appDir/qml`。

优点：

- 开发和发布路径完全一致。
- `main.cpp` 可以更简单。

缺点：

- 需要改多个 `qt_add_qml_module(OUTPUT_DIRECTORY ...)`。
- 可能影响插件 QML 类型注册、安装规则和生成器输出。
- 本阶段改动过大。

## 推荐方案

采用方案 B：**运行时路径优先，开发路径兜底**。

理由：

- 当前目标只是修复发布包运行时 QML import path，不应牵扯 QML 模块输出结构重排。
- 发布包可以通过运行时路径独立工作。
- 开发构建继续复用现有 `CMAKE_BINARY_DIR` 输出，风险小。

## 路径设计

新增一个小型路径收集函数，职责只限于返回候选 QML import path：

```cpp
static QStringList qmlImportPathCandidates(const QString& appDir)
```

候选顺序：

1. `appDir + "/qml"`
   - Linux 发布包：`bin/qml`
   - Windows 发布包：`qml`
   - 如果未来非 bundle macOS 也使用同级布局，也能覆盖。
2. `appDir + "/../Resources/qml"`
   - macOS bundle：`Contents/MacOS` 到 `Contents/Resources/qml`
3. `QML_IMPORT_PATH`
   - 开发构建兜底，通常是 `build`

添加时使用 `QDir::cleanPath()` 规范化路径，并只添加实际存在的目录。不存在的候选路径记录 debug 日志，最终添加的路径记录 info 日志。

如果没有任何候选路径存在，主程序不立即退出，因为根 QML 入口 `qrc:/PluginBased/qml/main.qml` 仍可能加载成功。但应记录 warn，提示插件 QML 模块、`AppLog` 或 `QuickUI` import 可能失败。

## 数据流

启动流程保持现有顺序：

1. 创建 `QGuiApplication`。
2. 初始化配置、日志和 CrashHandler。
3. 调用 `AppController::initPlugins()`，在 QML engine 创建前加载插件动态库。
4. 创建 `QQmlApplicationEngine`。
5. 添加运行时 QML import path。
6. 连接 QML warning 日志。
7. 加载 `qrc:/PluginBased/qml/main.qml`。

插件加载顺序不变，因为插件中的 QML 类型注册代码仍需要在 QML engine import 相关模块之前完成。

## 错误处理

路径处理规则：

- 候选路径不存在：debug 日志，继续检查下一个。
- 候选路径存在：调用 `engine.addImportPath(path)`，info 日志。
- 所有候选路径都不存在：warn 日志，但不提前退出。
- 同一路径重复出现：跳过重复添加。

示例日志：

```text
QML import path added: /Applications/PluginBasedApp.app/Contents/Resources/qml
QML import path missing, skipped: /Applications/PluginBasedApp.app/Contents/MacOS/qml
```

## 测试策略

新增或扩展 `tests/ci_ctest_checks.py`，做结构级检查：

1. `app/main.cpp` 不应只依赖 `QML_IMPORT_PATH`。
2. `main.cpp` 应包含运行时路径 `"/qml"`。
3. `main.cpp` 应包含 macOS bundle 路径 `"../Resources/qml"`。
4. `main.cpp` 应继续保留 `QML_IMPORT_PATH` 作为开发构建兜底。
5. `main.cpp` 应在 `engine.load(entryUrl)` 之前添加 import path。

人工验证：

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/app/PluginBasedApp
./package.sh --skip-build
python3 tools/verify.py --stage-dir <发布包 staging 目录>
```

如果可用，发布包应做一次真实启动 smoke test，确认没有 `module "QuickUI.Components" is not installed`、`module "AppLog" is not installed` 或插件模块 import 失败。

## 文件变更范围

计划修改：

- `app/main.cpp`
  - 增加运行时 QML import path 收集和添加逻辑。
  - 保留 `QML_IMPORT_PATH` 作为开发兜底。
  - 增加路径添加日志。
- `tests/ci_ctest_checks.py`
  - 增加结构检查，防止只依赖构建期 import path。
- `README.md` 或 `BUILD.md`
  - 如现有描述不完整，可补一句说明：开发构建使用构建目录兜底，发布包使用运行时 `qml/` 目录。

不修改：

- `tools/package.yml`
- `tools/deploy.py`
- 插件 CMake 输出目录
- 插件接口和 metadata schema

## 风险与处理

| 风险 | 处理 |
|---|---|
| 运行时路径添加顺序错误导致加载到旧模块 | 运行时路径优先，构建目录仅兜底 |
| macOS bundle 路径计算错误 | 使用 `appDir + "/../Resources/qml"` 并通过 `QDir::cleanPath()` 规范化 |
| 发布包缺少 qml 目录但开发机上构建目录存在，问题被掩盖 | 打包验证和发布包 smoke test 在干净环境执行 |
| 重复添加 import path | 使用 `QStringList` 去重 |
| 结构检查过于脆弱 | 只检查关键路径字符串和调用顺序，不绑定完整实现细节 |

## 验收标准

1. 开发构建仍可启动并加载主页、插件卡片和插件 QML 页面。
2. 发布包在没有构建目录的机器上能找到 `QuickUI`、`AppLog`、`PluginBased` 和插件 QML 模块。
3. 日志能显示实际添加了哪些 QML import path。
4. CTest 包含防退化检查。
5. 不引入插件 ABI、打包布局或 QML 模块输出目录变更。
