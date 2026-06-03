# PluginBased 完整改名设计

## 背景

项目已经从只服务视频播放器的工程，演进为通用插件宿主。`VideoPlayer` 这个名称继续出现在项目名、可执行文件、QML 模块、插件接口、打包脚本、配置路径和文档中，会让后续插件体系继续带有播放器语义。

本次目标是把当前工程完整改名为 `PluginBased`，让公开接口、运行时名称和打包产物都与通用插件管理方向一致。

## 范围

需要统一改名的内容：

- CMake 项目名：`VideoPlayer` -> `PluginBased`
- 主程序 target 和可执行文件：`VideoPlayerApp` -> `PluginBasedApp`
- 主 QML 模块 URI：`VideoPlayer 1.0` -> `PluginBased 1.0`
- 内部 CMake target：`VideoPlayerCore`、`VideoPlayerLogger`、`VideoPlayerPlugin` -> `PluginBasedCore`、`PluginBasedLogger`、`PluginBasedPlugin`
- 应用运行时名称：`VideoPlayer` -> `PluginBased`
- Qt 组织名：`MyOrg` -> `PluginBased`
- 配置文件：`videoplayer.ini` -> `pluginbased.ini`
- 日志文件：`videoplayer.log` -> `pluginbased.log`
- 插件 IID：`com.videoplayer.IAppPlugin/1.0` -> `com.pluginbased.IAppPlugin/1.0`
- 打包配置、脚本、产物名：`VideoPlayer-*` -> `PluginBased-*`
- README、BUILD、AGENTS、测试脚本和当前文档中的命令示例同步更新

不处理的内容：

- 不迁移旧的 `MyOrg/VideoPlayer` 数据目录或旧配置文件。
- 不编辑 `build/` 下的生成产物。
- 不编辑打包或压缩归档文件，例如 `plugins/PlayPlugin.zip`。
- 不处理未跟踪的本地文件 `6月29日.mov`。

## 设计

采用一次性完整改名，不保留旧命名兼容层。改名后，项目从源码、构建目标、QML import、插件元数据到发布产物都使用 `PluginBased` 语义。

主程序 target 改为 `PluginBasedApp` 后，插件安装路径、macOS bundle 路径、部署脚本和运行命令都跟随新 target 更新。QML 入口路径会随模块 URI 变为 `qrc:/PluginBased/qml/main.qml`，所有 `import VideoPlayer 1.0` 改为 `import PluginBased 1.0`。

插件接口层继续保持通用 `IAppPlugin` 设计，只替换 IID 命名空间和承载它的 CMake target 名称。现有 `DummyPlugin` 和 `PlayPlugin` 的 JSON 元数据同步使用 `com.pluginbased.IAppPlugin/1.0`，确保 `QPluginLoader` 仍能通过同一个通用接口加载插件。

日志和配置改名为 `pluginbased.log`、`pluginbased.ini`。首次运行会在新的 `PluginBased/PluginBased` 应用数据目录下生成新配置，不读取旧路径。

## 验证

验证以现有工程能力为准：

1. 更新并运行 `python3 tests/playplugin_regression_checks.py`，确认旧播放器接口、旧 IID 和旧 target 名不再作为当前接口出现。
2. 运行 `cmake --build build --parallel`，确认所有 target 和链接关系更新完整。
3. 如 build 缓存因 target 改名残留导致失败，重新 configure 现有 build 目录后再构建。
4. 启动 `./build/app/PluginBasedApp.app/Contents/MacOS/PluginBasedApp` 或平台对应可执行文件，确认宿主能加载 `DummyPlugin` 和 `PlayPlugin`。

## 风险

- target 改名会影响安装路径、脚本和文档示例，需要全局检查，避免遗漏旧可执行文件名。
- QML URI 改名会影响 import 和资源路径，遗漏会导致启动时找不到主 QML。
- 插件 IID 改名会让旧插件元数据不再兼容当前宿主。本项目当前插件会同步更新，因此不保留旧 IID。
- 旧用户配置不会迁移，符合本次保持命名干净的取舍。
