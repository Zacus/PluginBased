# PluginBased Rename Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rename the Qt/QML host project from `VideoPlayer` to `PluginBased` across source, build targets, plugin metadata, packaging, tests, and documentation.

**Architecture:** This is a naming refactor with no runtime compatibility layer. The host remains a generic `IAppPlugin` loader; only product, target, QML URI, IID, logging, configuration, and packaging names change. Existing plugins are updated in the same change so the host and bundled plugins stay compatible.

**Tech Stack:** CMake, Qt 6/QML, C++17, Python regression checks, shell packaging scripts.

---

## File Structure

- `CMakeLists.txt`: owns the top-level project name.
- `app/CMakeLists.txt`: owns the app target, host QML URI, QML output path, install examples, and link dependencies.
- `app/main.cpp`: owns Qt organization/application names, config filename, startup log text, QML import comments, and root QML URL.
- `app/AppController.h`, `app/AppConfig.h`, `app/AppController.cpp`: expose app name and path comments.
- `plugin/CMakeLists.txt`, `plugin/IAppPlugin.h`: own the generic plugin target and IID.
- `core/CMakeLists.txt`: owns the core interface target and dependencies on logger/plugin targets.
- `logger/CMakeLists.txt`, `logger/Logger.cpp`, `logger/QmlLogger.h`: own logger target name, log filename, and comments.
- `plugins/DummyPlugin/CMakeLists.txt`, `plugins/PlayPlugin/CMakeLists.txt`: link the renamed targets and install into the renamed app bundle target.
- `plugins/DummyPlugin/DummyPlugin.json`, `plugins/PlayPlugin/PlayPlugin.json`: carry the plugin IID string used by Qt metadata.
- `app/qml/main.qml`, `app/qml/HomePanel.qml`: import the renamed host QML module.
- `tools/package.yml`, `tools/deploy.py`, `package.sh`, `tools/verify.py`: own deployment metadata, binary names, bundle IDs, wrapper launchers, and stage discovery.
- `tests/playplugin_regression_checks.py`: acts as the source-level regression gate for this rename.
- `README.md`, `BUILD.md`, `AGENTS.md`: user-facing commands, diagrams, paths, and plugin contract docs.
- `docs/superpowers/specs/*.md`, `docs/superpowers/plans/*.md`: superpowers docs may keep old names when they describe previous work or explicit old-to-new rename mappings; active source, packaging, tests, README, BUILD, and AGENTS must use current names.

## Task 1: Update Regression Checks First

**Files:**
- Modify: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: Add PluginBased expectations before implementation**

Update existing IID and target checks from `VideoPlayer` to `PluginBased`, and add source-level checks for app, QML, packaging, and logging names:

```python
    root_cmake = read("CMakeLists.txt")
    app_cmake = read("app/CMakeLists.txt")
    core_cmake = read("core/CMakeLists.txt")
    logger_cmake = read("logger/CMakeLists.txt")
    main_cpp = read("app/main.cpp")
    app_controller_h = read("app/AppController.h")
    logger_cpp = read("logger/Logger.cpp")
    app_qml = read("app/qml/main.qml")
    home_panel_qml = read("app/qml/HomePanel.qml")
    package_yml = read("tools/package.yml")
    package_sh = read("package.sh")
    deploy_py = read("tools/deploy.py")

    require("project(PluginBased VERSION" in root_cmake,
            "top-level CMake project should be renamed to PluginBased")
    require("qt_add_executable(PluginBasedApp" in app_cmake,
            "host executable target should be PluginBasedApp")
    require('URI     "PluginBased"' in app_cmake,
            "host QML URI should be PluginBased")
    require('${CMAKE_BINARY_DIR}/PluginBased' in app_cmake,
            "host QML output directory should be PluginBased")
    require("PluginBasedLogger" in app_cmake and "PluginBasedPlugin" in app_cmake and
            "PluginBasedCore" in app_cmake,
            "host app should link renamed internal targets")
    require("PluginBasedCore" in core_cmake and "PluginBasedLogger" in core_cmake and
            "PluginBasedPlugin" in core_cmake,
            "core CMake target and dependencies should use PluginBased names")
    require("qt_add_qml_module(PluginBasedLogger" in logger_cmake,
            "logger target should be PluginBasedLogger")
    require('app.setOrganizationName("PluginBased")' in main_cpp,
            "Qt organization name should be PluginBased")
    require('app.setApplicationName("PluginBased")' in main_cpp,
            "Qt application name should be PluginBased")
    require('cfg.load(dataDir + "/config/pluginbased.ini")' in main_cpp,
            "config filename should be pluginbased.ini")
    require('qrc:/PluginBased/qml/main.qml' in main_cpp,
            "QML entry URL should use PluginBased")
    require('QStringLiteral("PluginBased")' in app_controller_h,
            "AppController should expose PluginBased as app name")
    require('"/pluginbased.log"' in logger_cpp,
            "logger should write pluginbased.log")
    require("import PluginBased 1.0" in app_qml and "import PluginBased 1.0" in home_panel_qml,
            "host QML files should import PluginBased 1.0")
    require("name: PluginBased" in package_yml and "binary: PluginBasedApp" in package_yml and
            "bundle_id: com.pluginbased.app" in package_yml,
            "packaging metadata should use PluginBased")
    require("PluginBasedApp" in package_sh and "project(PluginBased VERSION" in package_sh,
            "package.sh should discover the renamed project and app bundle")
    require('exec "${DIR}/bin/PluginBasedApp" "$@"' in deploy_py,
            "deployment wrapper should launch PluginBasedApp")
```

Change existing plugin interface assertions:

```python
    require("#define IAppPlugin_IID" in app_plugin_h and
            "com.pluginbased.IAppPlugin/1.0" in app_plugin_h,
            "IAppPlugin should expose the generic PluginBased plugin IID")
    require("PluginBasedPlugin" in plugin_cmake,
            "PluginBasedPlugin interface target should publish IAppPlugin.h")
    require("com.pluginbased.IAppPlugin/1.0" in playplugin_json,
            "PlayPlugin JSON metadata should use the generic PluginBased app plugin IID")
    require("com.pluginbased.IAppPlugin/1.0" in dummy_json,
            "DummyPlugin JSON metadata should use generic PluginBased app plugin IID")
```

- [ ] **Step 2: Run regression check and verify it fails**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: FAIL with an assertion mentioning `PluginBased`, because source files still contain `VideoPlayer` names.

- [ ] **Step 3: Commit the failing regression update**

Run:

```bash
git add tests/playplugin_regression_checks.py
git commit -m "[测试] 增加 PluginBased 改名回归检查"
```

Expected: one commit containing only the regression script update.

## Task 2: Rename Core Build Targets and Runtime Names

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `app/CMakeLists.txt`
- Modify: `core/CMakeLists.txt`
- Modify: `logger/CMakeLists.txt`
- Modify: `plugin/CMakeLists.txt`
- Modify: `app/main.cpp`
- Modify: `app/AppController.h`
- Modify: `app/AppConfig.h`
- Modify: `app/AppController.cpp`
- Modify: `logger/Logger.cpp`
- Modify: `logger/QmlLogger.h`
- Modify: `app/qml/main.qml`
- Modify: `app/qml/HomePanel.qml`

- [ ] **Step 1: Rename top-level and internal CMake targets**

Apply these source-level replacements in the listed files:

```text
CMakeLists.txt:
project(VideoPlayer VERSION 1.0.0 LANGUAGES CXX)
-> project(PluginBased VERSION 1.0.0 LANGUAGES CXX)

app/CMakeLists.txt:
VideoPlayerApp -> PluginBasedApp
VideoPlayerLogger -> PluginBasedLogger
VideoPlayerPlugin -> PluginBasedPlugin
VideoPlayerCore -> PluginBasedCore
URI     "VideoPlayer" -> URI     "PluginBased"
OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/VideoPlayer" -> OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/PluginBased"
foreach(mod AppLog VideoPlayer PlayPlugin) -> foreach(mod AppLog PluginBased PlayPlugin)

core/CMakeLists.txt:
VideoPlayerCore -> PluginBasedCore
VideoPlayerLogger -> PluginBasedLogger
VideoPlayerPlugin -> PluginBasedPlugin
VideoPlayerApp -> PluginBasedApp

logger/CMakeLists.txt:
VideoPlayerLogger -> PluginBasedLogger
libVideoPlayerLogger.a -> libPluginBasedLogger.a

plugin/CMakeLists.txt:
VideoPlayerPlugin -> PluginBasedPlugin
```

- [ ] **Step 2: Rename runtime app strings and QML module usage**

Apply these changes:

```text
app/main.cpp:
app.setOrganizationName("MyOrg") -> app.setOrganizationName("PluginBased")
app.setApplicationName("VideoPlayer") -> app.setApplicationName("PluginBased")
cfg.load(dataDir + "/config/videoplayer.ini") -> cfg.load(dataDir + "/config/pluginbased.ini")
LOG_INFO("=== VideoPlayer starting (v1.0.0) ===") -> LOG_INFO("=== PluginBased starting (v1.0.0) ===")
VideoPlayer/qmldir comment -> PluginBased/qmldir comment
VideoPlayer 1.0 comment -> PluginBased 1.0 comment
qrc:/VideoPlayer/qml/main.qml -> qrc:/PluginBased/qml/main.qml
LOG_INFO("=== VideoPlayer exiting ({}) ===", ret) -> LOG_INFO("=== PluginBased exiting ({}) ===", ret)

app/AppController.h:
QStringLiteral("VideoPlayer") -> QStringLiteral("PluginBased")

app/AppConfig.h:
videoplayer.ini -> pluginbased.ini

logger/Logger.cpp:
"/videoplayer.log" -> "/pluginbased.log"

logger/QmlLogger.h:
host's VideoPlayer 1.0 comment -> host's PluginBased 1.0 comment

app/qml/main.qml and app/qml/HomePanel.qml:
import VideoPlayer 1.0 -> import PluginBased 1.0
```

- [ ] **Step 3: Run the regression check**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: still FAIL because plugin IID, plugin CMake links, and packaging files are not all renamed yet.

- [ ] **Step 4: Commit core and runtime rename**

Run:

```bash
git add CMakeLists.txt app/CMakeLists.txt core/CMakeLists.txt logger/CMakeLists.txt plugin/CMakeLists.txt app/main.cpp app/AppController.h app/AppConfig.h app/AppController.cpp logger/Logger.cpp logger/QmlLogger.h app/qml/main.qml app/qml/HomePanel.qml
git commit -m "[功能修改] 项目核心命名改为 PluginBased"
```

Expected: one commit with CMake/runtime/QML naming changes.

## Task 3: Rename Plugin IID, Plugin Links, and Install Paths

**Files:**
- Modify: `plugin/IAppPlugin.h`
- Modify: `plugins/DummyPlugin/DummyPlugin.json`
- Modify: `plugins/PlayPlugin/PlayPlugin.json`
- Modify: `plugins/DummyPlugin/CMakeLists.txt`
- Modify: `plugins/PlayPlugin/CMakeLists.txt`
- Modify: `plugins/DummyPlugin/DummyPlugin.cpp`
- Modify: `plugins/DummyPlugin/DummyPlugin.h`
- Modify: selected `plugins/PlayPlugin/src/*.h` comment headers where `@FilePath` contains `/VideoPlayer/`

- [ ] **Step 1: Rename plugin IID and CMake link targets**

Apply these changes:

```text
plugin/IAppPlugin.h:
#define IAppPlugin_IID "com.videoplayer.IAppPlugin/1.0"
-> #define IAppPlugin_IID "com.pluginbased.IAppPlugin/1.0"

plugins/DummyPlugin/DummyPlugin.json:
"IID": "com.videoplayer.IAppPlugin/1.0"
-> "IID": "com.pluginbased.IAppPlugin/1.0"

plugins/PlayPlugin/PlayPlugin.json:
"IID": "com.videoplayer.IAppPlugin/1.0"
-> "IID": "com.pluginbased.IAppPlugin/1.0"

plugins/DummyPlugin/CMakeLists.txt:
VideoPlayerPlugin -> PluginBasedPlugin
VideoPlayerLogger -> PluginBasedLogger
VideoPlayerApp -> PluginBasedApp

plugins/PlayPlugin/CMakeLists.txt:
VideoPlayerPlugin -> PluginBasedPlugin
VideoPlayerLogger -> PluginBasedLogger
VideoPlayerCore -> PluginBasedCore in comments
VideoPlayerApp -> PluginBasedApp
```

- [ ] **Step 2: Rename source file path comments in touched plugin files**

Change `/VideoPlayer/` to `/PluginBased/` in comment headers only for files already being touched by this task:

```text
plugins/DummyPlugin/DummyPlugin.cpp
plugins/DummyPlugin/DummyPlugin.h
plugins/PlayPlugin/src/AudioRenderer.h
plugins/PlayPlugin/src/ClockSync.h
plugins/PlayPlugin/src/FFmpegSurface.h
```

- [ ] **Step 3: Run the regression check**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: FAIL only if packaging or docs still contain required old names; plugin IID checks should pass.

- [ ] **Step 4: Commit plugin rename**

Run:

```bash
git add plugin/IAppPlugin.h plugins/DummyPlugin/DummyPlugin.json plugins/PlayPlugin/PlayPlugin.json plugins/DummyPlugin/CMakeLists.txt plugins/PlayPlugin/CMakeLists.txt plugins/DummyPlugin/DummyPlugin.cpp plugins/DummyPlugin/DummyPlugin.h plugins/PlayPlugin/src/AudioRenderer.h plugins/PlayPlugin/src/ClockSync.h plugins/PlayPlugin/src/FFmpegSurface.h
git commit -m "[功能修改] 插件接口命名改为 PluginBased"
```

Expected: one commit covering plugin IID and plugin build integration.

## Task 4: Rename Packaging and Deployment Metadata

**Files:**
- Modify: `vcpkg.json`
- Modify: `package.sh`
- Modify: `tools/package.yml`
- Modify: `tools/deploy.py`
- Modify: `tools/verify.py`

- [ ] **Step 1: Update package metadata**

Apply these changes:

```text
vcpkg.json:
"name": "videoplayer" -> "name": "pluginbased"

tools/package.yml:
# VideoPlayer 打包配置 -> # PluginBased 打包配置
name: VideoPlayer -> name: PluginBased
binary: VideoPlayerApp -> binary: PluginBasedApp
bundle_id: com.myorg.videoplayer -> bundle_id: com.pluginbased.app
qml_modules entry VideoPlayer -> PluginBased
```

- [ ] **Step 2: Update package shell script**

Apply these changes in `package.sh`:

```text
# VideoPlayer 打包入口脚本 -> # PluginBased 打包入口脚本
project(VideoPlayer VERSION -> project(PluginBased VERSION
VideoPlayerApp.app -> PluginBasedApp.app
VideoPlayer-*-linux* -> PluginBased-*-linux*
```

- [ ] **Step 3: Update deploy and verify helper text**

Apply these changes:

```text
tools/deploy.py:
deploy.py — VideoPlayer 核心打包模块 -> deploy.py — PluginBased 核心打包模块
exec "${DIR}/bin/VideoPlayerApp" "$@" -> exec "${DIR}/bin/PluginBasedApp" "$@"
description="VideoPlayer 打包工具" -> description="PluginBased 打包工具"

tools/verify.py:
python3 tools/verify.py --stage-dir VideoPlayerApp.app --platform macos
-> python3 tools/verify.py --stage-dir PluginBasedApp.app --platform macos
```

- [ ] **Step 4: Run regression check**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: PASS if docs are the only remaining old-name references.

- [ ] **Step 5: Commit packaging rename**

Run:

```bash
git add vcpkg.json package.sh tools/package.yml tools/deploy.py tools/verify.py
git commit -m "[功能修改] 打包命名改为 PluginBased"
```

Expected: one commit with deployment metadata and helper script changes.

## Task 5: Update Current Documentation and Perform Global Old-Name Scan

**Files:**
- Modify: `README.md`
- Modify: `BUILD.md`
- Modify: `AGENTS.md`
- Modify: `docs/superpowers/specs/2026-06-03-pluginbased-rename-design.md`
- Modify: `docs/superpowers/plans/2026-06-03-pluginbased-rename.md`

- [ ] **Step 1: Update active project docs**

Apply these documentation replacements:

```text
README.md:
# VideoPlayer -> # PluginBased
VideoPlayer/ -> PluginBased/
VideoPlayerLogger -> PluginBasedLogger
VideoPlayerApp -> PluginBasedApp
videoplayer.ini -> pluginbased.ini

BUILD.md:
VideoPlayerApp -> PluginBasedApp
videoplayer.log -> pluginbased.log
videoplayer.ini -> pluginbased.ini
MyOrg/VideoPlayer -> PluginBased/PluginBased
VideoPlayer-<ver> -> PluginBased-<ver>
VideoPlayerPlugin -> PluginBasedPlugin
VideoPlayerLogger -> PluginBasedLogger
com.videoplayer.IAppPlugin/1.0 -> com.pluginbased.IAppPlugin/1.0

AGENTS.md:
/VideoPlayer/AGENTS.md -> /PluginBased/AGENTS.md
VideoPlayerApp -> PluginBasedApp
VideoPlayerPlugin -> PluginBasedPlugin
VideoPlayerLogger -> PluginBasedLogger
```

- [ ] **Step 2: Scan for old runtime/source names**

Run:

```bash
rg -n "VideoPlayer|VideoPlayerApp|videoplayer|com\\.videoplayer|MyOrg" -g '!build/**'
```

Expected: only superpowers design/plan files may contain old names when describing earlier work or this rename mapping. No active source, packaging, test, README, BUILD, or AGENTS file should contain old names.

- [ ] **Step 3: Run regression check**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: PASS.

- [ ] **Step 4: Commit documentation rename**

Run:

```bash
git add README.md BUILD.md AGENTS.md docs/superpowers/specs/2026-06-03-pluginbased-rename-design.md docs/superpowers/plans/2026-06-03-pluginbased-rename.md
git commit -m "[文档] 更新 PluginBased 项目命名"
```

Expected: one commit with active documentation and plan cleanup.

## Task 6: Configure, Build, and Smoke Verify

**Files:**
- No source edits expected.

- [ ] **Step 1: Build the renamed target**

Run:

```bash
cmake --build build --parallel
```

Expected: build succeeds and reports `PluginBasedApp`, `DummyPlugin`, and `PlayPlugin` targets.

- [ ] **Step 2: Reconfigure if stale CMake cache references the old target**

Only if Step 1 fails because generated build files still reference `VideoPlayerApp`, run the existing configure command with the same toolchain settings used for this workspace. Then run the build again:

```bash
cmake --build build --parallel
```

Expected: build succeeds after regeneration.

- [ ] **Step 3: Launch the renamed app for a smoke test**

On macOS, run:

```bash
./build/app/PluginBasedApp.app/Contents/MacOS/PluginBasedApp
```

Expected: the app starts, creates data under the new `PluginBased/PluginBased` app data location, and loads `DummyPlugin` and `PlayPlugin`.

- [ ] **Step 4: Check renamed log path**

Run:

```bash
tail -n 80 "$HOME/Library/Application Support/PluginBased/PluginBased/logs/pluginbased.log"
```

Expected: log contains `PluginBased starting`, plugin load entries for `DummyPlugin` and `PlayPlugin`, and no startup failure for `qrc:/PluginBased/qml/main.qml`.

- [ ] **Step 5: Final old-name scan**

Run:

```bash
rg -n "VideoPlayer|VideoPlayerApp|videoplayer|com\\.videoplayer|MyOrg" -g '!build/**'
```

Expected: only superpowers docs with historical context or explicit rename mappings may remain. If active files still contain old runtime names, fix them before reporting verification.

- [ ] **Step 6: Report verification**

Summarize these results in the final response:

```text
python3 tests/playplugin_regression_checks.py: PASS
cmake --build build --parallel: PASS
PluginBasedApp smoke launch: PASS or explain blocker
Old-name scan: active files clean or list intentional historical references
```
