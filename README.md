# VideoPlayer — Qt/QML 视频播放器框架

## 项目结构

```
VideoPlayer/
├── CMakeLists.txt                  # 顶层构建文件
├── README.md
│
├── app/                            # 主程序
│   ├── main.cpp                    # 入口：日志初始化、Dump注册、QML引擎启动
│   ├── AppController.h/.cpp        # 应用级单例（qmlRegisterSingletonType）
│   ├── CrashHandler.h/.cpp         # Windows MiniDump / Linux core dump
│   └── Logger.h/.cpp               # spdlog 封装
│
├── core/                           # 核心业务
│   ├── PlayerEngine.h/.cpp         # 播放引擎（qmlRegisterType）
│   ├── MediaInfo.h/.cpp            # 媒体信息模型（qmlRegisterType）
│   ├── PlaylistModel.h/.cpp        # 播放列表（QAbstractListModel）
│   └── PluginManager.h/.cpp        # 插件管理器单例
│
├── plugin/                         # 插件接口
│   ├── IPlayerPlugin.h             # 纯虚插件接口
│   └── PlayerPluginLoader.h/.cpp   # 动态库加载器
│
├── plugins/                        # 具体插件实现（示例）
│   └── DummyPlugin/
│       ├── CMakeLists.txt
│       └── DummyPlugin.h/.cpp
│
└── qml/                            # QML UI
    ├── main.qml                    # 根窗口
    ├── PlayerView.qml              # 播放器主视图
    ├── ControlBar.qml              # 控制栏
    ├── PlaylistView.qml            # 播放列表
    └── components/
        ├── IconButton.qml
        └── ProgressSlider.qml
```

## 依赖

| 库 | 版本 | 用途 |
|---|---|---|
| Qt | 6.5+ | 框架 |
| spdlog | 1.12+ | 日志 |
| fmt | 10+ | 格式化（spdlog依赖） |

## 构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## 设计要点

- `qmlRegisterSingletonType<AppController>` — 全局应用状态（替代全局变量注入）
- `qmlRegisterSingletonType<PluginManager>` — 插件管理器
- `qmlRegisterType<PlayerEngine>` — 可在QML中实例化的播放引擎
- `qmlRegisterType<PlaylistModel>` — 播放列表数据模型
- `qmlRegisterUncreatableType<MediaInfo>` — 只读媒体信息（由C++创建）
- 插件通过 `IPlayerPlugin` 接口实现，运行时动态加载 `.dll/.so`
- Dump：Windows用 `SetUnhandledExceptionFilter + MiniDumpWriteDump`，Linux用 `signal(SIGSEGV/SIGABRT)`
