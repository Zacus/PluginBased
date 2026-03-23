# VideoPlayer

基于 Qt6 / QML 的插件化视频播放器框架。以**应用面板**为主页，插件以卡片形式展示，点击进入；内置播放器、播放列表管理，支持运行时动态加载 `.dll/.so/.dylib` 插件。

---

## 目录结构

```
VideoPlayer/
├── CMakeLists.txt               # 顶层构建
├── README.md
├── BUILD.md                     # 详细构建 & 打包说明
├── package.sh                   # 一键打包脚本（macOS/Linux/Windows）
│
├── app/                         # 主程序
│   ├── main.cpp                 # 入口：读配置 → 初始化日志 → 启动 QML
│   ├── AppConfig.h/.cpp         # INI 配置管理（QSettings，零额外依赖）
│   ├── AppController.h/.cpp     # 应用级单例，QML_SINGLETON
│   ├── CrashHandler.h/.cpp      # Crash 捕获（Windows MiniDump / Linux signal）
│   └── qml/
│       ├── main.qml             # 根窗口 + StackView 导航
│       ├── HomePanel.qml        # 应用主页（插件卡片面板）
│       ├── PlayerView.qml       # 播放器视图
│       ├── ControlBar.qml       # 播放控制栏
│       └── PlaylistView.qml     # 播放列表侧边栏
│
├── core/                        # 核心业务
│   ├── PlayerEngine.h/.cpp      # 播放引擎，QML_ELEMENT
│   ├── MediaInfo.h              # 媒体信息模型
│   ├── PlaylistModel.h/.cpp     # 播放列表（QAbstractListModel）
│   └── PluginManager.h/.cpp     # 插件管理器，QML_SINGLETON
│
├── logger/                      # 共享日志库（VideoPlayerLogger）
│   ├── Logger.h/.cpp            # spdlog 封装，供 core/plugin 使用
│   └── CMakeLists.txt
│
├── plugin/                      # 插件接口定义
│   └── IPlayerPlugin.h          # 纯虚接口
│
└── plugins/                     # 插件实现
    └── DummyPlugin/             # 示例插件
```

---

## 依赖

| 库 | 版本 | 用途 |
|---|---|---|
| Qt | 6.5+ | 框架：Core / Gui / Quick / QuickControls2 / Multimedia |
| spdlog | 1.12+ | 结构化日志 |
| fmt | 10+ | 格式化（spdlog 依赖） |

---

## 快速构建

```bash
# 安装依赖（vcpkg）
vcpkg install spdlog fmt

# 配置 & 构建
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --parallel

# 运行
./build/app/VideoPlayerApp
```

详细说明、Release 构建、打包步骤见 [BUILD.md](BUILD.md)。

---

## 主要设计

### 导航模型

主页 `HomePanel` 作为 `StackView` 的初始页，卡片点击后 `push` 对应视图页面，顶部栏自动出现 `←` 返回按钮。新增视图只需向 `stack.push()` 传入新的 `Component`，无需修改路由逻辑。

### 配置文件

首次运行自动生成 `<AppLocalDataLocation>/config/videoplayer.ini`，支持运行时热重载（调用 `AppController.reloadConfig()`）：

```ini
[log]
level            = debug    ; trace | debug | info | warn | error | critical | off
dir              = logs
max_file_size_mb = 5
max_files        = 3
flush_on         = warn
```

### 插件系统

实现 `IPlayerPlugin` 接口，编译为动态库，放入 `plugins/` 目录即可被自动扫描加载。卡片面板会在 `pluginsReady` 信号触发后动态渲染插件卡片。

### QML 类型注册

| 类型 | 注册方式 | QML 用法 |
|---|---|---|
| `AppController` | `QML_SINGLETON` | `AppController.quit()` |
| `PluginManager` | `QML_SINGLETON` | `PluginManager.pluginCount` |
| `PlayerEngine` | `QML_ELEMENT` | `PlayerEngine { }` |
| `PlaylistModel` | `QML_ELEMENT` | `PlaylistModel { }` |
| `MediaInfo` | `QML_UNCREATABLE` | 只读，由 C++ 创建 |

### Crash 捕获

| 平台 | 机制 | 产物 |
|---|---|---|
| Windows | `SetUnhandledExceptionFilter` + `MiniDumpWriteDump` | `dumps/crash_YYYYMMDD_HHMMSS.dmp` |
| Linux | `signal(SIGSEGV/SIGABRT)` + backtrace | `dumps/crash_YYYYMMDD_HHMMSS.log` |

---

## 开发插件

1. 在 `plugins/` 下新建目录，`CMakeLists.txt` 使用 `add_library(MyPlugin MODULE ...)`
2. 继承 `IPlayerPlugin`，实现所有纯虚方法
3. 在类声明中加入：
   ```cpp
   Q_OBJECT
   Q_INTERFACES(IPlayerPlugin)
   Q_PLUGIN_METADATA(IID IPlayerPlugin_IID FILE "MyPlugin.json")
   ```
4. 编译产物放入 `plugins/` 目录，`PluginManager::loadAll()` 在启动时自动扫描

参考实现：`plugins/DummyPlugin/`
