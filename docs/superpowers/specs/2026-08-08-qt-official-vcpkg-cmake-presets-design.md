# Qt 官方二进制、vcpkg 与 CMake Presets 构建设计

## 背景

项目当前在顶层 `CMakeLists.txt` 中要求 Qt 6.8，并通过 vcpkg manifest 安装
`spdlog`、`ffmpeg` 和 `pkgconf`。vcpkg 已固定在
`ea1a7396b05637a53bf23c078647ecc0edee4b80`，三个直接依赖也已精确锁定。

本地配置仍需要手写 `CMAKE_TOOLCHAIN_FILE`，Qt 安装位置也由调用者单独传入。
macOS CI 则通过 Homebrew 安装未固定 patch 版本的 Qt，并维护一段 Qt 路径探测与
重装脚本。结果是本地与 CI 没有统一的配置入口，CI 使用的 Qt 版本会随 Homebrew
变化，且每次配置命令需要重复表达相同参数。

项目链接 `Qt6::GuiPrivate`，私有 Qt API 对 patch 版本比普通公开 API 更敏感，
因此明确统一 Qt patch 版本比只要求 Qt 6.8 更稳妥。

## 目标

- 使用 Qt 官方预编译二进制，并把项目构建版本精确固定为 Qt `6.8.3`。
- 继续由 vcpkg 管理并锁定 `spdlog 1.17.0#0`、`ffmpeg 8.0.1#2` 和
  `pkgconf 2.5.1#4`，不改变现有 feature 集合。
- 使用 `CMakePresets.json` 统一本地与 CI 的 configure、build 和 test 入口。
- 删除 macOS CI 的 Homebrew Qt 安装及路径猜测逻辑，改用固定版本的
  `aqtinstall 3.3.0` 获取 Qt 官方包。
- 保留 C++17、现有插件 ABI、目标结构、构建目录约定和测试诊断产物。
- 让缺失或错误的 Qt/vcpkg 环境在 configure 阶段明确失败，不静默回退到系统依赖。

## 非目标

- 不通过 vcpkg 构建 Qt，也不把 Qt ports 加入 `vcpkg.json`。
- 不在本次变更中新增 Windows 或 Linux CI job；Presets 本身保持跨平台可用。
- 不提交开发机绝对路径或生成 `CMakeUserPresets.json`。
- 不升级现有非 Qt 依赖，不修改业务 C++、QML、播放、插件或打包行为。
- 不引入 Conan、Qt Maintenance Tool 自动化或第三方 Qt 安装 GitHub Action。

## 当前设计

本地和 CI 分别拼装 CMake 参数：

```text
本地 Qt / Homebrew Qt
          +
手写 CMAKE_PREFIX_PATH、Qt6_DIR
          +
手写 vcpkg toolchain 路径
          |
          v
cmake -S . -B build ...
```

CI 的 Qt 安装步骤同时负责安装、重装、搜索 `Qt6Config.cmake` 和导出多个环境变量，
其职责过多。configure、build、test 又各自直接引用 `build`，没有一份可供本地复用
的声明式配置。

## 方案

### 依赖职责边界

依赖来源按构建特性拆分：

| 依赖 | 来源与版本 | 选择理由 |
|---|---|---|
| Qt | Qt 官方二进制 `6.8.3` | 避免本地和 CI 重复源码编译大型 Qt 依赖图 |
| spdlog | vcpkg `1.17.0#0` | 延续现有 manifest 锁定 |
| ffmpeg | vcpkg `8.0.1#2` | 延续现有 features、pkg-config 和链接方式 |
| pkgconf | vcpkg `2.5.1#4` | 延续现有 FFmpeg 发现方式 |
| aqtinstall | PyPI `3.3.0`，仅 CI 安装工具 | 以命令行方式下载 Qt 官方预编译包 |

Qt 6.8.3 的 macOS `clang_64` 基础包提供 `qtbase`、`qtdeclarative`、`qttools`、
`qtsvg` 和 `qttranslations`；CI 额外安装项目需要的 `qtmultimedia` 与
`qtshadertools`。不安装 `-m all`，避免无关模块增加下载和缓存体积。

顶层 `find_package` 改为要求 `Qt6 6.8.3 EXACT`。现有
`qt_standard_project_setup(REQUIRES 6.8)` 保持不变：前者负责依赖版本精确性，
后者继续声明项目采用的 Qt policy 基线。

### CMake Presets

根目录新增 schema version 3 的 `CMakePresets.json`，与项目现有
`cmake_minimum_required(VERSION 3.21)` 对齐。文件只保存可提交的相对规则，不保存
机器路径。

Presets 读取两个调用环境变量：

- `VCPKG_ROOT`：固定 vcpkg checkout 的根目录；
- `QT_ROOT`：Qt 安装前缀，即包含 `lib/cmake/Qt6/Qt6Config.cmake` 的
  `6.8.3/macos`、`gcc_64` 或对应 Windows kit 目录。

隐藏的基础 configure preset 负责设置：

- `toolchainFile` 为
  `$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake`；
- `CMAKE_PREFIX_PATH` 为 `$env{QT_ROOT}`；
- `Qt6_DIR` 为 `$env{QT_ROOT}/lib/cmake/Qt6`，避免 Preset 构建绕过指定 kit；
- `BUILD_TESTING=ON`。

公开 presets 为：

| 类型 | 名称 | 构建目录 | 配置 |
|---|---|---|---|
| configure | `debug` | `${sourceDir}/build` | `Debug` |
| configure | `release` | `${sourceDir}/build-release` | `Release` |
| build | `debug` / `release` | 继承同名 configure preset | 对应配置 |
| test | `debug` / `release` | 继承同名 configure preset | `outputOnFailure=true` |

不固定 generator，让 CMake 在各平台继续选择原生默认 generator；build/test preset
同时声明 configuration，使单配置和多配置 generator 都能使用同一命令。

标准入口为：

```bash
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug
```

Release 使用相同命令并把 preset 名替换为 `release`。

### macOS CI

`.github/workflows/ci.yml` 继续使用 `macos-14`，并保留现有仓库 checkout、vcpkg
完整历史 checkout、vcpkg 下载缓存、binary cache、configure/test 诊断和失败产物。
job 环境预先设置 `VCPKG_ROOT=${{ github.workspace }}/vcpkg` 与
`QT_ROOT=${{ runner.temp }}/Qt/6.8.3/macos`，供验证步骤和 Presets 共同使用。

Qt 流程改为：

1. 使用 `actions/setup-python@v5` 提供 Python 3.12；
2. 用 pip 安装精确版本 `aqtinstall==3.3.0`；
3. 缓存 `${{ runner.temp }}/Qt/6.8.3`，cache key 包含 Qt 版本、架构、模块集合和
   aqtinstall 版本；
4. cache miss 时运行
   `aqt install-qt --outputdir "${RUNNER_TEMP}/Qt" mac desktop 6.8.3 clang_64 -m qtmultimedia qtshadertools`；
5. 验证 `$QT_ROOT/lib/cmake/Qt6/Qt6Config.cmake` 存在；
6. 分别运行 `cmake --preset debug`、
   `cmake --build --preset debug --parallel` 和 `ctest --preset debug`。

Qt 缓存即使命中也必须执行文件存在性验证，避免损坏或不完整缓存被当作有效安装。
configure 失败时继续上传 `configure.log`、vcpkg manifest log 和 CMake configure
日志；CTest 失败时继续上传 `ctest.log` 与 `LastTest.log`。

### 文档

更新 `README.md` 和 `BUILD.md`，记录：

- Qt `6.8.3` 的官方安装要求和两个必需附加模块；
- `VCPKG_ROOT`、`QT_ROOT` 的含义及 macOS/Linux/Windows 路径示例；
- Debug、Release 的 Preset 命令；
- vcpkg 固定版本和 Qt 固定版本各自的升级入口；
- 直接 CMake 参数只作为排障信息，不再作为推荐构建方式。

## API、ABI 与兼容性

该方案只改变构建依赖选择与调用入口，不改变公开头文件、插件接口、符号可见性、
二进制布局或运行时配置。C++ 标准保持 C++17。

Qt 从“任意 6.8 或更高版本”收紧为“精确 6.8.3”，这是有意的构建兼容性约束。
现有 macOS CI 使用 Qt 官方 universal `clang_64` 包；Windows 和 Linux 开发者需安装
同版本、与其编译器匹配的官方 desktop kit。vcpkg triplet 仍由 vcpkg/CMake 按平台
选择，本次不写死 triplet。

## 错误与回退

- `VCPKG_ROOT` 未设置或指向错误目录时，CMake 必须因 toolchain 文件不存在而失败。
- `QT_ROOT` 未设置、版本不是 6.8.3、缺少 Multimedia/ShaderTools/LinguistTools 等
  组件时，`find_package` 必须失败并列出缺失内容。
- CI Qt 缓存恢复后若 `Qt6Config.cmake` 不存在，Qt 验证步骤必须失败，不能退回
  Homebrew Qt 或 runner 预装 Qt。
- aqtinstall 下载失败时保留命令的非零退出码；不自动放宽 Qt 版本或改装最新版本。
- 回退应整体还原 Presets、CI Qt 安装、Qt EXACT 约束和文档，不能只恢复其中一处。

## 备选方案

### Qt 与全部第三方库都由 vcpkg 管理

单一包管理器看起来更统一，但 Qt 会从源码构建，依赖图大、CI 冷启动慢。要把
Qt 6.8.3 固定在当前 baseline 上，还需要管理多个 Qt port 的版本闭包。收益不足以
抵消构建时间和维护复杂度，因此不采用。

### 系统 Qt 或 Homebrew Qt 加 CMake Presets

Presets 能统一命令，但 Qt patch 版本仍随机器或 Homebrew 更新，无法解决项目使用
`GuiPrivate` 时最重要的版本一致性问题，因此不采用。

### Qt 官方安装器加手写 CMake 命令

依赖来源合理，但本地与 CI 仍会复制 toolchain、Qt prefix、build type 和测试参数，
容易继续漂移，因此不采用。

## 验证

1. 先扩展 `tests/ci_ctest_checks.py`，解析真实 `CMakePresets.json` 并验证：
   schema、CMake 版本下限、环境变量引用、Debug/Release 构建目录以及对应
   configure/build/test preset 关系。
2. 运行该检查并确认在 Presets 尚未创建时按预期失败，再创建 Presets 使其通过。
3. 扩展同一检查，验证 CI 固定 Qt `6.8.3`、aqtinstall `3.3.0`、最小模块集合、
   Qt 配置文件验证和三条 Preset 命令，同时确认不再安装 Homebrew Qt。
4. 验证顶层 CMake 精确要求 Qt 6.8.3，且现有 vcpkg baseline、overrides、features
   与完整 checkout 历史约束保持不变。
5. 运行 `cmake --list-presets`，确认 CMake 能读取并列出 `debug`、`release`。
6. 在 `VCPKG_ROOT` 与 `QT_ROOT` 指向有效安装时执行 Debug configure、build 和完整
   `ctest --preset debug`。
7. 若本机没有 Qt 6.8.3 官方 kit，则仍完成静态配置检查和现有不依赖重新配置的
   CTest，并在交付中明确记录未执行的精确 Qt configure 路径；CI 作为官方安装的
   端到端验证入口。

## 发布与升级

本次变更不需要运行时迁移。Qt 升级必须在一个变更中同步更新：顶层 CMake EXACT
版本、aqtinstall 命令、Qt cache key、`QT_ROOT`、Presets 回归检查和文档。vcpkg
依赖升级继续同步更新 `vcpkg.json` baseline/overrides 与 CI checkout ref。

实现时只修改本设计列出的构建、CI、测试和文档文件，并保留工作区内现有播放与
Seek 相关未提交改动。
