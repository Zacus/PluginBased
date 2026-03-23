# 构建说明

## 快速开始

### 安装依赖（vcpkg 方式）

```bash
vcpkg install spdlog fmt qt6
```

### 构建

```bash
git clone <repo>
cd VideoPlayer
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --parallel
```

### 运行

```bash
./build/app/VideoPlayerApp
```

---

## QML 类型注册总览

| 注册方式 | 类型 | QML 名称 | 用途 |
|---|---|---|---|
| `qmlRegisterSingletonType` | `AppController` | `AppController` | 全局应用状态、日志、退出 |
| `qmlRegisterSingletonType` | `PluginManager` | `PluginManager` | 查询已加载插件 |
| `qmlRegisterType` | `PlayerEngine` | `PlayerEngine { }` | 播放引擎，可在QML实例化 |
| `qmlRegisterType` | `PlaylistModel` | `PlaylistModel { }` | 播放列表数据模型 |
| `qmlRegisterUncreatableType` | `MediaInfo` | `MediaInfo` | 只读媒体信息（C++创建） |

---

## 插件开发指南

1. 新建 `MyPlugin/` 目录，CMakeLists 使用 `add_library(MyPlugin MODULE ...)`
2. 继承 `IPlayerPlugin`，实现所有纯虚方法
3. 添加：
   ```cpp
   Q_OBJECT
   Q_INTERFACES(IPlayerPlugin)
   Q_PLUGIN_METADATA(IID IPlayerPlugin_IID FILE "MyPlugin.json")
   ```
4. 编译输出放入 `plugins/` 目录，`PluginManager::loadAll("plugins")` 自动扫描加载

---

## Dump 文件位置

| 平台 | Dump 类型 | 默认路径 |
|---|---|---|
| Windows | `.dmp` MiniDump | `dumps/crash_YYYYMMDD_HHMMSS.dmp` |
| Linux | `.log` backtrace | `dumps/crash_YYYYMMDD_HHMMSS.log` |

用 WinDbg / VS 打开 `.dmp` 文件调试；Linux 另配合 `ulimit -c unlimited` 获取 core dump。

---

## 日志文件

`logs/videoplayer.log`（滚动，最大 5MB × 3 个文件）

格式：`[2025-01-01 12:00:00.123] [info] [12345] 消息内容`
