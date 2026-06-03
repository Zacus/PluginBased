# 构建 & 打包说明

## 环境要求

| 工具 | 最低版本 | 说明 |
|---|---|---|
| CMake | 3.21 | 构建系统 |
| C++ 编译器 | C++17 | GCC 11 / Clang 14 / MSVC 2022 |
| Qt | 6.5 | 通过 Qt Installer 或 vcpkg 安装 |
| spdlog | 1.12 | 通过 vcpkg 安装 |
| vcpkg | 任意 | 推荐依赖管理方式 |

---

## 安装依赖

### 方式一：vcpkg（推荐）

```bash
# 安装 vcpkg（如未安装）
git clone https://github.com/microsoft/vcpkg
./vcpkg/bootstrap-vcpkg.sh   # Windows: bootstrap-vcpkg.bat

# 项目根目录已有 vcpkg.json（manifest 模式），
# cmake 配置时会自动安装所有依赖：spdlog、pkgconf、ffmpeg
# 无需手动执行 vcpkg install
```

### 方式二：系统包管理器（Linux）

```bash
# Ubuntu / Debian
sudo apt install qt6-base-dev qt6-declarative-dev qt6-multimedia-dev \
                 libspdlog-dev libfmt-dev

# Arch
sudo pacman -S qt6-base qt6-declarative qt6-multimedia spdlog
```

### 方式三：Homebrew（macOS）

```bash
brew install qt spdlog
```

---

## 构建

### Debug 构建（开发期）

```bash
cmake -B build \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build build --parallel
```

### Release 构建（发布前）

```bash
cmake -B build-release \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build build-release --parallel
```

### Qt 路径提示

若 CMake 找不到 Qt6，手动指定：

```bash
cmake -B build -DQt6_DIR=~/Qt/6.7.2/macos/lib/cmake/Qt6 ...
# 或设置环境变量
export Qt6_DIR=~/Qt/6.7.2/gcc_64/lib/cmake/Qt6
```

---

## 运行

```bash
# Debug
./build/app/VideoPlayerApp

# Release
./build-release/app/VideoPlayerApp
```

首次运行会在 `<AppLocalDataLocation>` 下自动创建：

```
logs/        — 日志文件（videoplayer.log，滚动 5MB × 3）
dumps/       — Crash dump 文件
config/      — 配置文件（videoplayer.ini）
```

各平台 `AppLocalDataLocation` 路径：

| 平台 | 路径 |
|---|---|
| macOS | `~/Library/Application Support/MyOrg/VideoPlayer/` |
| Linux | `~/.local/share/MyOrg/VideoPlayer/` |
| Windows | `C:\Users\<User>\AppData\Local\MyOrg\VideoPlayer\` |

---

## 配置文件

首次运行自动生成，路径为 `<AppLocalDataLocation>/config/videoplayer.ini`：

```ini
[log]
level            = debug    ; trace | debug | info | warn | error | critical | off
dir              = logs     ; 相对路径（基于 AppLocalDataLocation）或绝对路径
max_file_size_mb = 5        ; 单个日志文件最大 MB
max_files        = 3        ; 滚动保留文件数
flush_on         = warn     ; 达到此级别立即 flush 到磁盘
```

修改后无需重启，在 QML 中调用即可热重载：

```qml
AppController.reloadConfig()
```

---

## 打包发布

使用项目根目录的 `package.sh`，自动调用平台对应的 Qt 部署工具：

```bash
# 确保已完成 Release 构建，直接打包
./package.sh --skip-build

# 同时触发编译（要求 build/ 已 cmake 初始化）
./package.sh

# 指定构建目录和 Qt 路径
./package.sh ./build-release --qt-dir ~/Qt/6.7.2/macos

# 覆盖版本号
./package.sh --skip-build --version 1.2.0
```

### 各平台产物

| 平台 | 产物 | 依赖工具 |
|---|---|---|
| macOS | `dist/VideoPlayer-<ver>-macOS.dmg` | `macdeployqt`（Qt 自带）、`hdiutil`（系统自带）|
| Linux | `dist/VideoPlayer-<ver>-linux-x86_64.tar.gz` | `linuxdeployqt`（可选，否则手动收集）|
| Linux | `dist/VideoPlayer-<ver>-linux-x86_64.AppImage` | `appimagetool`（可选）|
| Windows | `dist/VideoPlayer-<ver>-win64.zip` | `windeployqt`（Qt 自带）|
| Windows | `dist/VideoPlayer-<ver>-win64-installer.exe` | NSIS `makensis`（可选）|

### macOS 注意事项

项目通过 `FetchContent` 引入 `QtQuickComponents` 子库，编译时该库的 rpath 只含构建目录，`macdeployqt` 会报 `Cannot resolve rpath @rpath/QtXxx.framework`。`package.sh` 在调用 `macdeployqt` 前会自动用 `install_name_tool` 修复所有 Mach-O 的 rpath，无需手动处理。

---

## QML 类型注册总览

| 注册方式 | C++ 类型 | QML 名称 | 用途 |
|---|---|---|---|
| `QML_SINGLETON` | `AppController` | `AppController` | 应用状态、日志、退出、配置热重载 |
| `QML_SINGLETON` | `PluginManager` | `PluginManager` | 查询已加载插件 |
| `QML_ELEMENT` | `PlayerEngine` | `PlayerEngine { }` | 播放引擎，可在 QML 实例化 |
| `QML_ELEMENT` | `PlaylistModel` | `PlaylistModel { }` | 播放列表数据模型 |
| `QML_UNCREATABLE` | `MediaInfo` | `MediaInfo` | 只读媒体信息（C++ 侧创建） |

---

## 插件开发

### 步骤

1. 在 `plugins/` 下新建目录，编写 `CMakeLists.txt`：

```cmake
add_library(MyPlugin MODULE
    MyPlugin.h
    MyPlugin.cpp
)
target_link_libraries(MyPlugin PRIVATE
    VideoPlayerPlugin   # IAppPlugin 接口库
    VideoPlayerLogger   # 日志库
    Qt6::Core
)
set_target_properties(MyPlugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)
```

2. 继承 `IAppPlugin` 通用插件接口，在类声明中加入元数据：

```cpp
class MyPlugin : public QObject, public IAppPlugin
{
    Q_OBJECT
    Q_INTERFACES(IAppPlugin)
    Q_PLUGIN_METADATA(IID IAppPlugin_IID FILE "MyPlugin.json")
public:
    QString id()          const override { return "my-plugin"; }
    QString name()        const override { return "MyPlugin"; }
    QString version()     const override { return "1.0.0"; }
    QString description() const override { return "我的插件"; }
    // ...
};
```

`IAppPlugin` 是宿主加载插件的唯一接口。插件可以提供 QML 页面，也可以在插件内部实现自己的业务能力；宿主不会为播放器、转码器或其他具体领域定义专用插件接口。

3. 编写 `MyPlugin.json`：

```json
{
    "IID": "com.videoplayer.IAppPlugin/1.0",
    "MetaData": {
        "name":    "MyPlugin",
        "version": "1.0.0"
    }
}
```

4. 在顶层 `CMakeLists.txt` 加入 `add_subdirectory(plugins/MyPlugin)`，编译后插件出现在 `build/plugins/`，首页面板会自动显示对应卡片。

参考实现：`plugins/DummyPlugin/`

---

## Crash 分析

### Windows

用 WinDbg 或 Visual Studio 打开 `.dmp` 文件：

```
File → Open Crash Dump → 选择 dumps/crash_YYYYMMDD_HHMMSS.dmp
```

### Linux

程序捕获 `SIGSEGV`/`SIGABRT` 后写入 backtrace 日志。也可用系统 core dump 配合 GDB：

```bash
ulimit -c unlimited          # 开启 core dump
./VideoPlayerApp             # 崩溃后生成 core 文件
gdb ./VideoPlayerApp core    # 加载调试
(gdb) bt                     # 打印调用栈
```

---

## 日志

文件位置：`<AppLocalDataLocation>/logs/videoplayer.log`（滚动，最大 5 MB × 3 个文件）

格式：

```
[2025-01-01 12:00:00.123] [info] [12345] 消息内容
```

级别从低到高：`trace` < `debug` < `info` < `warn` < `error` < `critical`

生产环境建议将 `level` 设为 `info` 或 `warn` 以减少磁盘占用。
