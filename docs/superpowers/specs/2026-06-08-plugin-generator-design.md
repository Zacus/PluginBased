# 插件结构生成器设计

## 背景

当前新增插件需要手工创建插件目录、编写 `CMakeLists.txt`、实现 `IAppPlugin`、补齐 metadata 和可选 QML 页面。即使复制一个完整插件目录到 `plugins/` 下，顶层 `CMakeLists.txt` 仍需要手工添加 `add_subdirectory(plugins/<PluginName>)`，否则不会参与构建。

目标是提供一个可视化的独立开发工具：开发者运行插件生成器界面，填写插件信息，选择“带 QML 页面”或“No-QML 后台插件”，点击生成后得到符合当前宿主约定的插件目录。目录复制到 `plugins/` 后，重新配置/构建即可被编译，并在运行时由 `PluginManager` 扫描加载。

## 目标

1. 新增一个 Qt/QML 独立工具 `PluginGeneratorApp`。
2. 工具界面支持输入插件名、显示名、描述、文字图标、图片图标和输出目录。
3. 工具界面支持选择插件类型：
   - 带 QML 页面
   - No-QML 后台插件
4. 生成器必须拒绝非法插件名和已存在目录，避免覆盖用户文件。
5. 顶层 CMake 自动发现 `plugins/*/CMakeLists.txt`，不再为每个插件手工写 `add_subdirectory`。
6. 更新 README 中的插件开发说明，改为推荐使用可视化生成器。

## 非目标

1. 不实现插件商店、安装器或热加载。
2. 不生成复杂业务代码，只生成最小可编译模板。
3. 不修改 `IAppPlugin` 接口。
4. 不把生成器集成进主程序 `PluginBasedApp`。
5. 不自动提交、推送或修改用户未跟踪文件。

## 工具形态

生成器是一个独立 Qt/QML 可执行程序：

```text
tools/plugin_generator/
├── CMakeLists.txt
├── main.cpp
├── PluginTemplateGenerator.h
├── PluginTemplateGenerator.cpp
└── qml/
    └── Main.qml
```

构建产物：

```text
build/tools/plugin_generator/PluginGeneratorApp
```

运行方式：

```bash
cmake --build build --target PluginGeneratorApp --parallel
./build/tools/plugin_generator/PluginGeneratorApp
```

生成器不依赖主程序运行时，不加载任何业务插件。它只复用项目已有 Qt、CMake、`IAppPlugin` 模板约定。

## 界面设计

主界面为一个表单式工具窗口，尺寸约 `720 x 520`，使用项目当前 Qt/QML 技术栈。

字段：

| 字段 | 控件 | 默认值 | 说明 |
|---|---|---|---|
| 插件名 | `TextField` | 空 | 必填，作为 C++ 类名、CMake target、QML URI 和目录名 |
| 显示名 | `TextField` | 跟随插件名 | 作为首页卡片名称和 QML `pageTitle` |
| 描述 | `TextArea` | 空 | 写入 `description()` |
| 文字图标 | `TextField` | `⬡` | 未选择图片时写入 `cardIcon()` 作为 fallback |
| 图片图标 | `TextField` + 选择图片按钮 + 预览 | 空 | 选择本地图片后复制到生成插件的 `assets/` 目录，并写入 `cardIconUrl()` |
| 输出目录 | `TextField` + 浏览按钮 | `<repo>/plugins` | 生成插件目录的父目录 |
| 插件类型 | 二选一分段控件或 RadioButton | 带 QML 页面 | 选择 QML / No-QML |

操作：

1. 用户填写插件名。
2. 用户选择插件类型。
3. 用户点击“生成插件”。
4. 界面显示成功消息和生成路径，或显示具体错误。

错误提示：

- 插件名为空。
- 插件名非法。
- 图片图标路径不存在。
- 输出目录不存在且无法创建。
- 目标插件目录已存在。
- 文件写入失败。

## 后端设计

`PluginTemplateGenerator` 是 QML 可调用的 C++ 后端类，负责校验输入和写文件。

核心 API：

```cpp
class PluginTemplateGenerator : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    Q_INVOKABLE QVariantMap generate(const QVariantMap& options);
};
```

`options` 字段：

| 字段 | 类型 | 说明 |
|---|---|---|
| `pluginName` | string | 插件名 |
| `displayName` | string | 卡片和页面标题 |
| `description` | string | 插件描述 |
| `icon` | string | 文字卡片图标 fallback |
| `iconPath` | string | 可选图片图标源文件路径 |
| `outputDir` | string | 输出父目录 |
| `withQml` | bool | 是否生成 QML 页面 |

返回值：

```qml
{
    "ok": true,
    "path": "/abs/path/to/plugins/MyPlugin",
    "message": "插件已生成"
}
```

失败时：

```qml
{
    "ok": false,
    "message": "Invalid plugin name: 123Plugin"
}
```

## 插件名规则

插件名同时作为 C++ 类名、CMake target 名、QML URI 和目录名，因此必须保守校验：

```text
^[A-Za-z][A-Za-z0-9_]*$
```

示例：

| 输入 | 结果 |
|---|---|
| `MyPlugin` | 允许 |
| `CameraToolPlugin` | 允许 |
| `my_plugin` | 允许 |
| `123Plugin` | 拒绝 |
| `My Plugin` | 拒绝 |
| `My-Plugin` | 拒绝 |

插件 ID 自动从插件名转换为 kebab-case：

| 插件名 | plugin id |
|---|---|
| `MyPlugin` | `my-plugin` |
| `CameraToolPlugin` | `camera-tool-plugin` |
| `my_plugin` | `my-plugin` |

## 默认 QML 插件输出结构

```text
MyPlugin/
├── CMakeLists.txt
├── MyPlugin.h
├── MyPlugin.cpp
├── MyPlugin.json
├── assets/
│   └── icon.png                 # 仅选择图片图标时生成
└── qml/
    └── MyPluginView.qml
```

`MyPlugin.h`：

- 继承 `QObject, IAppPlugin`
- 声明 `Q_OBJECT`
- 声明 `Q_INTERFACES(IAppPlugin)`
- 声明 `Q_PLUGIN_METADATA(IID IAppPlugin_IID FILE "MyPlugin.json")`
- 实现 `id()`、`name()`、`version()`、`description()`、`cardIcon()`、`cardName()`
- 如果选择图片图标，额外覆盖 `cardIconUrl()`
- 声明 `initialize()`、`shutdown()`
- QML 插件额外覆盖 `hasQmlUI()` 和 `qmlComponentUrl()`

`MyPlugin.cpp`：

- `initialize()` 写日志并返回 `true`
- `shutdown()` 写日志
- QML 插件返回 `qrc:/MyPlugin/qml/MyPluginView.qml`
- 如果选择图片图标，返回 `qrc:/MyPlugin/assets/icon.<ext>`

`MyPlugin.json`：

```json
{
    "IID": "com.pluginbased.IAppPlugin/1.0",
    "MetaData": {
        "name": "MyPlugin",
        "version": "1.0.0"
    }
}
```

`qml/MyPluginView.qml`：

- 根对象为 `Item`
- 提供 `property string pageTitle`
- 提供简单占位 UI，确认插件页面已加载

`CMakeLists.txt`：

- 使用 `add_library(MyPlugin MODULE ...)`
- QML 插件使用 `qt_add_qml_module(MyPlugin NO_PLUGIN URI MyPlugin VERSION 1.0 ...)`
- 如果选择图片图标，使用 `qt_add_resources(MyPlugin ... PREFIX "/MyPlugin" FILES assets/icon.<ext>)`
- 输出到 `${CMAKE_BINARY_DIR}/plugins`
- 链接 `Qt6::Core`、`PluginBasedPlugin`、`PluginBasedLogger`
- QML 插件额外链接 `Qt6::Quick`、`QtQuickComponents`
- 保留 macOS 和非 macOS 安装规则

## No-QML 后台插件输出结构

```text
MyPlugin/
├── CMakeLists.txt
├── MyPlugin.h
├── MyPlugin.cpp
├── assets/
│   └── icon.png                 # 仅选择图片图标时生成
└── MyPlugin.json
```

No-QML 后台插件不生成 `qml/`，不调用 `qt_add_qml_module()`，不覆盖 `hasQmlUI()` 和 `qmlComponentUrl()`，依赖 `IAppPlugin` 默认实现。

如果 No-QML 后台插件选择了图片图标，仍然会生成 `assets/icon.<ext>`、覆盖 `cardIconUrl()`，并通过 `qt_add_resources()` 嵌入图片资源。这样后台插件也能在首页卡片上显示图片图标。

## 首页图片图标渲染

宿主插件接口新增可选图片图标：

```cpp
virtual QUrl cardIconUrl() const { return QUrl{}; }
```

`PluginManager` 增加：

```cpp
Q_INVOKABLE QUrl pluginCardIconUrl(int index) const;
```

`HomePanel.qml` 中 `PluginCard` 增加 `property url iconUrl`。当 `iconUrl` 非空时使用 `Image` 渲染图片；为空时继续使用现有 `cardIcon()` 文本图标 fallback。现有插件不需要修改即可保持原显示效果。

## CMake 自动发现插件

顶层 `CMakeLists.txt` 将从手写：

```cmake
add_subdirectory(plugins/DummyPlugin)
add_subdirectory(plugins/PlayPlugin)
```

改为自动扫描：

```cmake
file(GLOB PLUGIN_CMAKELISTS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/plugins/*/CMakeLists.txt"
)

foreach(_plugin_cmake IN LISTS PLUGIN_CMAKELISTS)
    get_filename_component(_plugin_dir "${_plugin_cmake}" DIRECTORY)
    add_subdirectory("${_plugin_dir}")
endforeach()
```

使用 `CONFIGURE_DEPENDS` 让 CMake 在新增插件目录后重新配置时能发现变化。

顶层 CMake 还需要加入：

```cmake
add_subdirectory(tools/plugin_generator)
```

## 测试策略

新增 `tests/plugin_generator_checks.py`，通过直接编译/调用 `PluginTemplateGenerator` 的生成逻辑或运行一个轻量测试入口验证行为：

1. 默认 QML 插件生成完整结构。
2. 默认 QML 插件内容包含正确 class、metadata、CMake target、QML resource URL。
3. 选择图片图标时，生成器复制图片到 `assets/icon.<ext>`，生成 `cardIconUrl()`，并在 CMake 中嵌入资源。
4. No-QML 插件不生成 `qml/`，不生成 `qt_add_qml_module()`。
5. 非法插件名返回失败，并且不创建文件。
6. 目标目录已存在时返回失败，并且不覆盖已有文件。
7. 顶层 CMake 不再包含手写 `plugins/DummyPlugin` / `plugins/PlayPlugin`，而是包含自动扫描逻辑。
8. `HomePanel.qml` 支持图片图标和文字图标 fallback。

构建验证：

```bash
cmake --build build --target PluginGeneratorApp --parallel
cmake --build build --parallel
```

继续保留并运行现有：

```bash
python3 tests/playplugin_regression_checks.py
```

可视化验证：

1. 启动 `PluginGeneratorApp`。
2. 输入 `VisualTestPlugin`。
3. 选择“带 QML 页面”，生成到临时目录。
4. 确认界面显示成功路径。
5. 切换到 No-QML，再生成另一个插件。
6. 确认界面显示成功路径，并且生成目录没有 `qml/`。

## 执行顺序

1. 新增失败测试 `tests/plugin_generator_checks.py`，覆盖模板生成和 CMake 自动发现约束。
2. 运行测试，确认失败原因是 `PluginGeneratorApp` 后端和自动 CMake 发现尚未实现。
3. 新增 `tools/plugin_generator/PluginTemplateGenerator.h/.cpp`。
4. 新增 `tools/plugin_generator/main.cpp` 和 `qml/Main.qml`。
5. 新增 `tools/plugin_generator/CMakeLists.txt`。
6. 更新顶层 `CMakeLists.txt`：加入插件自动发现和生成器工具目录。
7. 更新 `README.md` 插件开发说明。
8. 运行新旧回归测试。
9. 运行项目构建。

## 风险与处理

| 风险 | 处理 |
|---|---|
| 自动扫描引入非插件目录 | 只扫描含 `CMakeLists.txt` 的 `plugins/*/` 一级目录 |
| 插件名导致 C++/QML/CMake 名称非法 | 使用严格正则校验 |
| 覆盖用户已有插件 | 如果目标目录存在直接失败 |
| 图片文件不存在或不可读 | 生成前校验 `iconPath`，失败时不创建插件目录 |
| 图片图标破坏老插件 | `cardIconUrl()` 默认返回空 URL，首页保留 `cardIcon()` fallback |
| QML 插件生成 Qt QML plugin 入口导致符号冲突 | 模板固定使用 `qt_add_qml_module(... NO_PLUGIN ...)` |
| 生成器和主程序职责混在一起 | 生成器作为独立 `PluginGeneratorApp`，不集成进 `PluginBasedApp` |
| 可视化界面难以自动测试 | 生成逻辑放在 C++ 后端，测试后端输出；界面只做薄表单 |
| 文档仍提示手工改顶层 CMake | README 同步更新为复制到 `plugins/` 后重新配置/构建 |
