# 构建 & 打包说明

## 环境要求

| 工具 | 最低版本 | 说明 |
|---|---|---|
| CMake | 3.24 | 构建系统（打包使用 `--fresh`） |
| Ninja | 1.10 | Debug/Release Preset 的固定生成器 |
| C++ 编译器 | C++17 | GCC 11 / Clang 14 / MSVC 2022 |
| Qt | 6.8.3（精确版本） | 通过 Qt 官方安装器安装，不由此 vcpkg manifest 提供 |
| spdlog | 1.17.0#0 | 通过 vcpkg 安装 |
| vcpkg | ea1a7396 | 必须与 `builtin-baseline` 保持一致 |

---

## 安装依赖

推荐先运行统一的环境准备工具：

```bash
python3 tools/setup_build_environment.py --configure debug
```

工具优先复用现有 `VCPKG_ROOT`、`QT_ROOT` 和 `~/vcpkg/vcpkg-master` 中的
Git checkout 与 downloads 缓存；缺少依赖时才安装固定版本，然后直接
配置仓库自带的 `debug` Preset。它不生成任何本机专用的 Preset 文件。

### 1. Qt 官方预编译包

通过 Qt 官方安装器安装 Desktop Qt `6.8.3`。基础 kit 已包含 Core、Gui、Quick、
QuickControls2、Test 与 LinguistTools；另外选择以下模块：

- `qtmultimedia`
- `qtshadertools`

项目使用 `Qt6::GuiPrivate`，因此顶层 CMake 精确要求 6.8.3，不会接受其他 Qt
patch 版本。CI 使用固定的 `aqtinstall 3.3.0` 下载相同官方包，不通过 Homebrew
安装 Qt。

### 2. vcpkg

```bash
# 安装 vcpkg（如未安装）
git clone https://github.com/microsoft/vcpkg "$HOME/vcpkg/vcpkg-master"
git -C "$HOME/vcpkg/vcpkg-master" checkout ea1a7396b05637a53bf23c078647ecc0edee4b80
"$HOME/vcpkg/vcpkg-master/bootstrap-vcpkg.sh"   # Windows: bootstrap-vcpkg.bat
```

项目通过 `vcpkg.json` 精确锁定直接依赖：`spdlog 1.17.0#0`、
`ffmpeg 8.0.1#2`、`pkgconf 2.5.1#4`。`builtin-baseline` 同时固定传递依赖的
版本解析快照。

| 依赖 | 版本 | 用途 |
|---|---|---|
| spdlog | 1.17.0#0 | 结构化日志 |
| fmt | 由固定 baseline 解析 | spdlog 的格式化依赖 |
| FFmpeg | 8.0.1#2 | Media SDK demux/decode、重采样、像素转换 |
| pkgconf/pkg-config | 2.5.1#4 | CMake 查找 FFmpeg pkg-config 模块 |

升级 vcpkg 依赖时，必须在同一个变更中同步更新 `vcpkg.json` 的
`builtin-baseline`/`overrides` 与 `.github/workflows/ci.yml` 的 vcpkg checkout
`ref`，然后重新执行配置、构建和 CTest。不要只更新其中一处。

### 3. 可选的路径覆盖

仓库内的 `cmake/PluginBasedToolchain.cmake` 会按平台默认使用：

- vcpkg：`~/vcpkg/vcpkg-master`
- macOS Qt：`~/Qt/6.8.3/macos`
- Linux Qt：`~/Qt/6.8.3/gcc_64`
- Windows Qt：`~/Qt/6.8.3/msvc2022_64`

只有在依赖不在默认位置时，才需要设置以下环境变量：

```bash
# macOS
export VCPKG_ROOT=/path/to/vcpkg
export QT_ROOT="$HOME/Qt/6.8.3/macos"

# Linux
export VCPKG_ROOT=/path/to/vcpkg
export QT_ROOT="$HOME/Qt/6.8.3/gcc_64"
```

```powershell
# Windows / PowerShell
$env:VCPKG_ROOT = "C:\src\vcpkg"
$env:QT_ROOT = "C:\Qt\6.8.3\msvc2022_64"
```

`QT_ROOT` 必须是包含 `lib/cmake/Qt6/Qt6Config.cmake` 的 kit 根目录；
`VCPKG_ROOT` 必须是已 checkout 到项目固定提交并完成 bootstrap 的 vcpkg 根目录。

---

## 构建

### Debug 构建（开发期）

```bash
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug
```

Preset 不保存个人路径，Debug/Release 均固定使用 Ninja。

### Release 构建（发布前）

```bash
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release
```

### 直接配置（仅用于排障）

日常构建应使用 Presets。需要检查变量展开时，可显式执行等价配置：

```bash
cmake -S . -B build \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_TOOLCHAIN_FILE=cmake/PluginBasedToolchain.cmake
```

Qt 升级必须在同一变更中同步更新 `CMakeLists.txt` 的 EXACT 版本、CI 的
aqtinstall 命令、Qt cache key、`QT_ROOT`、`tests/ci_ctest_checks.py` 和本文档。

---

## 运行

```bash
# Debug
./build/app/PluginBasedApp

# Release
./build-release/app/PluginBasedApp
```

首次运行会在 `<AppLocalDataLocation>` 下自动创建：

```
logs/        — 日志文件（pluginbased.log，滚动 5MB × 3）
dumps/       — Crash dump 文件
config/      — 配置文件（pluginbased.ini）
```

各平台 `AppLocalDataLocation` 路径：

| 平台 | 路径 |
|---|---|
| macOS | `~/Library/Application Support/PluginBased/PluginBased/` |
| Linux | `~/.local/share/PluginBased/PluginBased/` |
| Windows | `C:\Users\<User>\AppData\Local\PluginBased\PluginBased\` |

---

## 配置文件

首次运行自动生成，路径为 `<AppLocalDataLocation>/config/pluginbased.ini`：

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
# 自动 configure/build release Preset 并打包
./package.sh

# 已完成 release Preset 构建，只执行打包
./package.sh --skip-build

# 查看 CMake 定义的应用版本
./package.sh --version

# 打包调用方管理的 Release 构建目录
./package.sh --skip-build /path/to/release-build
```

默认打包入口始终使用仓库的 `release` Preset，构建目录为
`build-release/`，每次先清理整个默认 Release 生成目录（包括 FetchContent 子构建），
再使用 `cmake --preset release --fresh` 配置，消除旧生成器、旧工具链和旧 Qt cache 带来的
不确定性。传入位置参数时，脚本保留调用方
管理的构建目录支持，但该目录必须包含 Release 配置。

打包使用 `CMakeCache.txt` 中的 `PLUGINBASED_QT_ROOT`/`Qt6_DIR` 作为 Qt 唯一
真实来源。`QT_DIR` 或 `--qt-dir` 只用于显式校验，与构建 Qt 不一致时立即失败，
不会混用 Homebrew Qt 的部署工具或插件。发布版本只来自 CMake `project(VERSION)`；
`--version` 仅查询版本，不接受覆盖值。打包配置为 `tools/package.json`，仅依赖
Python 标准库。

归档前，打包器会在持久化 staging 目录上执行 `tools/verify.py`，任何缺失依赖或
非系统绝对路径都会阻止归档。`--no-verify` 只用于本地诊断，不应用于发布。

### 产品版本与构建身份

产品版本只有一个人工维护来源：根目录 `CMakeLists.txt` 中的
`project(PluginBased VERSION 1.0.0)`。发布下一版本时只修改这里；
`./package.sh --version`、平台原生版本字段、应用“关于”窗口和包名都会读取该值。

配置和构建会生成以下文件，它们属于构建输出，不应提交：

- `<build>/generated/BuildInfoData.h`：编译进应用的只读构建身份；
- `<build>/build-info.json`：供打包、校验和问题诊断使用的同源数据。

普通本地构建允许工作树为 dirty；缺少 Git 元数据时会明确标记为 `unknown`。
正式发布必须使用干净、已知且与产品版本匹配的 `vMAJOR.MINOR.PATCH` tag：

```bash
cmake --preset release --fresh \
  -DPLUGINBASED_OFFICIAL_BUILD=ON \
  -DPLUGINBASED_EXPECTED_TAG=v1.0.0
```

严格校验会拒绝未知提交、dirty 工作树、错误 tag 或 tag/产品版本不一致。构建身份
只保存产品版本、提交、源码状态和构建工具信息；分支名、用户名、主机名、源码路径
及远程仓库地址永远不会写入头文件、JSON、日志或复制到剪贴板。GitHub tag 发布使用
同一套正式构建校验，校验通过后才允许归档和上传。

### 各平台产物

| 平台 | 产物 | 依赖工具 |
|---|---|---|
| macOS | `dist/PluginBased-<ver>-macOS.dmg` | `macdeployqt`（Qt 自带）、`hdiutil`（系统自带）|
| Linux | `dist/PluginBased-<ver>-linux-x86_64.tar.gz` | `ldd`、`patchelf` |
| Windows | `dist/PluginBased-<ver>-win64.zip` | `windeployqt`（Qt 自带）|

Linux 和 Windows 都从主程序、业务插件、Qt/QML 插件出发递归收集运行时
依赖闭包，未解析的 FFmpeg、日志库或 Qt 传递依赖会直接使打包失败。可检查的
staging 分别位于 `build-release/_package_macos`、`_package_linux`和 `_package_windows`。

### macOS 注意事项

项目通过 `FetchContent` 自动获取固定提交的 `QtQuickComponents`。打包器先运行
`macdeployqt`，再补齐 QML/插件的传递 framework 与 dylib，统一改写 install name/rpath、
重新签名并验证，任何 `install_name_tool` 失败都会终止发布。

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
    PluginBasedPlugin   # IAppPlugin 接口库
    PluginBasedLogger   # 日志库
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
    "IID": "com.pluginbased.IAppPlugin/1.0",
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
./PluginBasedApp             # 崩溃后生成 core 文件
gdb ./PluginBasedApp core    # 加载调试
(gdb) bt                     # 打印调用栈
```

---

## 日志

文件位置：`<AppLocalDataLocation>/logs/pluginbased.log`（滚动，最大 5 MB × 3 个文件）

格式：

```
[2025-01-01 12:00:00.123] [info] [12345] 消息内容
```

级别从低到高：`trace` < `debug` < `info` < `warn` < `error` < `critical`

生产环境建议将 `level` 设为 `info` 或 `warn` 以减少磁盘占用。
