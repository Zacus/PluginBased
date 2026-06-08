# PluginBased

基于 Qt 6 / QML 的通用插件化应用宿主。以**应用面板**为主页，插件以卡片形式展示，点击进入；业务能力由插件提供，支持运行时动态加载 `.dll/.so` 插件。

---

## 目录结构

```
PluginBased/
├── CMakeLists.txt               # 顶层构建
├── README.md
├── BUILD.md                     # 详细构建 & 打包说明
├── package.sh                   # 一键打包脚本（macOS/Linux/Windows）
│
├── app/                         # 主程序
│   ├── main.cpp                 # 入口：读配置 → 初始化日志 → 加载插件 → 启动 QML
│   ├── AppConfig.h/.cpp         # INI 配置管理（QSettings，零额外依赖）
│   ├── AppController.h/.cpp     # 应用级单例，QML_SINGLETON
│   ├── CrashHandler.h/.cpp      # Crash 捕获（Windows MiniDump / Unix signal）
│   └── qml/
│       ├── main.qml             # 根窗口 + StackView 导航
│       ├── HomePanel.qml        # 应用主页（插件卡片面板）
│
├── core/                        # 宿主核心接口
│   ├── PluginManager.h/.cpp     # 插件管理器，QML_SINGLETON
│   └── CMakeLists.txt           # PluginBasedCore 接口目标
│
├── logger/                      # 共享日志库（PluginBasedLogger）
│   ├── Logger.h/.cpp            # spdlog 封装，供 core/plugin 使用
│   ├── QmlLogger.h/.cpp         # QML 日志桥接
│   └── CMakeLists.txt
│
├── plugin/                      # 插件接口定义
│   └── IAppPlugin.h             # 通用插件接口
│
├── plugins/                     # 插件实现
│   ├── DummyPlugin/             # 最小示例插件
│   └── PlayPlugin/              # 自包含视频播放器插件
│       ├── PlayPlugin.h/.cpp    # IAppPlugin 实现
│       ├── qml/                 # 播放器界面、控制栏、播放列表
│       ├── shaders/             # YUV 视频渲染 shader
│       └── src/                 # FFmpeg 解码、音视频渲染、硬件解码后端
│
├── tests/                       # 轻量回归检查脚本
└── tools/                       # 打包、部署、依赖验证工具
```

---

## 依赖

| 库 | 版本 | 用途 |
|---|---|---|
| Qt | 6.5+ | 框架：Core / Gui / Quick / QuickControls2 / Multimedia |
| spdlog | 1.12+ | 结构化日志 |
| fmt | 10+ | 格式化（spdlog 依赖） |
| FFmpeg | 5.0+ | PlayPlugin 音视频解码、重采样、像素转换 |
| pkgconf/pkg-config | 任意 | CMake 查找 FFmpeg pkg-config 模块 |

项目顶层通过 `FetchContent` 引入相邻目录 `../QtQuickComponents`，构建前请确保该目录存在，或在 `CMakeLists.txt` 中替换为对应的远程仓库来源。

---

## 快速构建

```bash
# 配置（vcpkg manifest 会自动安装 spdlog、fmt、pkgconf、ffmpeg）
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake

# 构建
cmake --build build --parallel

# 运行
./build/app/PluginBasedApp

# 轻量回归检查
python3 tests/playplugin_regression_checks.py
```

详细说明、Release 构建、打包步骤见 [BUILD.md](BUILD.md)。

---

## 主要设计

### 导航模型

主页 `HomePanel` 作为 `StackView` 的初始页，卡片点击后 `push` 通用插件页面容器。插件若声明 `hasQmlUI()`，宿主会通过 `Loader` 加载插件返回的 `qmlComponentUrl()`；顶部栏根据 `StackView` 深度自动显示返回按钮。

### 配置文件

首次运行自动生成 `<AppLocalDataLocation>/config/pluginbased.ini`，支持运行时热重载（调用 `AppController.reloadConfig()`）：

```ini
[log]
level            = debug    ; trace | debug | info | warn | error | critical | off
dir              = logs
max_file_size_mb = 5
max_files        = 3
flush_on         = warn
```

### 插件系统

实现 `IAppPlugin` 通用插件接口，编译为动态库，放入构建或发布包的 `plugins/` 目录即可被自动扫描加载。卡片面板会在 `pluginsReady` 信号触发后动态渲染插件卡片。

宿主只负责插件加载、生命周期和页面路由；播放器、转码器或其他具体业务能力都由插件内部实现。当前 `PlayPlugin` 是一个自包含播放器插件，内部包含 FFmpeg 解码、硬件解码后端、音频渲染、视频渲染、播放列表模型和 QML 界面。

### QML 类型注册

| 类型 | 所属模块 | 注册方式 | QML 用法 |
|---|---|---|---|
| `AppController` | `PluginBased 1.0` | `QML_SINGLETON` | `AppController.quit()` |
| `PluginManager` | `PluginBased 1.0` | `QML_SINGLETON` | `PluginManager.pluginCount` |
| `PlayerEngine` | `PlayPlugin 1.0` | `QML_ELEMENT` | `PlayerEngine { }` |
| `PlaylistModel` | `PlayPlugin 1.0` | `QML_ELEMENT` | `PlaylistModel { }` |
| `FFmpegSurface` | `PlayPlugin 1.0` | `QML_ELEMENT` | 播放器视频输出项 |
| `PlaybackContext` | `PlayPlugin 1.0` | `QML_SINGLETON` | 插件内部共享播放上下文 |
| `MediaInfo` | `PlayPlugin 1.0` | `QML_UNCREATABLE` | 只读，由 C++ 创建 |

### Crash 捕获

| 平台 | 机制 | 产物 |
|---|---|---|
| Windows | `SetUnhandledExceptionFilter` + `MiniDumpWriteDump` | `dumps/crash_YYYYMMDD_HHMMSS.dmp` |
| Linux / macOS | `signal(SIGSEGV/SIGABRT)` + backtrace | `dumps/crash_YYYYMMDD_HHMMSS.log` |

---

## 开发插件

`IAppPlugin` 是宿主加载插件的唯一接口，负责元信息、生命周期、主页卡片和可选 QML 页面。插件可以在内部实现自己的业务能力；宿主不会为播放器、转码器或其他具体领域定义专用插件接口。

推荐使用可视化生成器创建插件骨架：

```bash
cmake --build build --target PluginGeneratorApp --parallel
./build/tools/plugin_generator/PluginGeneratorApp
```

在界面中填写插件名、显示名、描述、文字图标、可选图片图标和输出目录，并选择插件类型。选择图片图标时，生成器会把图片复制到插件 `assets/` 目录并嵌入 Qt resource；未选择图片时会使用文字图标作为卡片 fallback。

| 类型 | 说明 |
|---|---|
| 带 QML 页面 | 生成 `qml/<PluginName>View.qml`，首页卡片点击后会打开插件页面 |
| No-QML 后台插件 | 只生成 C++ 插件骨架，不提供可打开页面 |

生成后的目录复制或直接生成到 `plugins/` 下即可。顶层 CMake 会自动扫描 `plugins/*/CMakeLists.txt`，新增插件后重新配置/构建即可参与编译。

手工创建插件时需要保持以下约定：

1. 在 `plugins/` 下新建目录，`CMakeLists.txt` 使用 `add_library(MyPlugin MODULE ...)`
2. 插件继承 `IAppPlugin`，在类声明中加入：
   ```cpp
   Q_OBJECT
   Q_INTERFACES(IAppPlugin)
   Q_PLUGIN_METADATA(IID IAppPlugin_IID FILE "MyPlugin.json")
   ```
3. 插件实现元信息、生命周期和可选 QML 页面：
   ```cpp
   QString id() const override { return "my-plugin"; }
   QString name() const override { return "MyPlugin"; }
   QString version() const override { return "1.0.0"; }
   QString description() const override { return "我的插件"; }

   bool initialize() override;
   void shutdown() override;

   bool hasQmlUI() const override { return true; }
   QUrl qmlComponentUrl() const override {
       return QUrl(QStringLiteral("qrc:/MyPlugin/qml/MyPluginView.qml"));
   }
   ```
4. 编写插件元数据文件：
   ```json
   {
     "IID": "com.pluginbased.IAppPlugin/1.0",
     "MetaData": {
       "name": "MyPlugin",
       "version": "1.0.0"
     }
   }
   ```
5. 编译后插件产物放入 `build/plugins/`，`PluginManager::loadAll()` 在启动时自动扫描。

参考实现：`plugins/DummyPlugin/`
