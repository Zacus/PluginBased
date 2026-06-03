# Remove Player Plugin Interface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove `IPlayerPlugin` so the host only knows about generic `IAppPlugin` instances.

**Architecture:** `PluginManager` loads and exposes only `IAppPlugin`. `PluginContext` becomes a generic, currently empty host context. PlayPlugin keeps all playback behavior inside its own QML/C++ module through `PlayerEngine`.

**Tech Stack:** C++17, Qt 6 plugin interfaces, CMake, QML, existing Python text-level regression checks.

---

## File Structure

- Delete `plugin/IPlayerPlugin.h`: removes host-level player capability contract.
- Modify `plugin/IAppPlugin.h`: remove `IPlayerPlugin` forward declaration and player finder from `PluginContext`.
- Modify `plugin/CMakeLists.txt`: publish only `IAppPlugin.h`.
- Modify `core/PluginManager.h/.cpp`: remove `IPlayerPlugin` include and `findPlayerPlugin()`.
- Modify `plugins/PlayPlugin/PlayPlugin.h/.cpp`: implement only `IAppPlugin`; remove host-facing playback methods.
- Modify `plugins/PlayPlugin/src/PlaybackContext.h`: remove host-provided player finder; keep PlayPlugin-local engine and pending URL state.
- Modify docs and comments mentioning `IPlayerPlugin`.
- Modify `tests/playplugin_regression_checks.py`: assert `IPlayerPlugin` is gone and host/player coupling does not reappear.

---

### Task 1: Make Regression Checks Require No Player Interface

**Files:**
- Modify: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: Replace player capability checks**

In `tests/playplugin_regression_checks.py`, replace the current `iplayer_h = read("plugin/IPlayerPlugin.h")` block and all checks requiring `IPlayerPlugin` with these checks:

```python
def file_exists(path):
    return (ROOT / path).exists()

require(not file_exists("plugin/IPlayerPlugin.h"),
        "IPlayerPlugin interface should be removed from the host plugin contract")
require("IPlayerPlugin" not in app_plugin_h,
        "IAppPlugin should not mention player-specific interfaces")
require("PlayerPluginFinder" not in app_plugin_h and "findPlayerPlugin" not in app_plugin_h,
        "PluginContext should not expose player-specific callbacks")
require("struct PluginContext" in app_plugin_h,
        "IAppPlugin should keep a generic PluginContext extension point")
require("IPlayerPlugin" not in plugin_cmake,
        "VideoPlayerPlugin target should not publish IPlayerPlugin.h")
```

- [ ] **Step 2: Replace manager checks**

Update manager checks to:

```python
require('#include "IAppPlugin.h"' in manager_h,
        "PluginManager should include the generic app plugin interface")
require("IPlayerPlugin" not in manager_h and "IPlayerPlugin" not in manager_cpp,
        "PluginManager should not mention player-specific interfaces")
require("findPlayerPlugin" not in manager_h and "findPlayerPlugin" not in manager_cpp,
        "PluginManager should not expose player capability lookup")
require("qobject_cast<IAppPlugin*>" in manager_cpp,
        "PluginManager should load generic app plugins")
require("PluginContext context" in manager_cpp,
        "PluginManager should still pass generic PluginContext into app plugin initialization")
```

- [ ] **Step 3: Replace PlayPlugin and docs checks**

Update PlayPlugin and docs checks to:

```python
require("#include \"IAppPlugin.h\"" in playplugin_h,
        "PlayPlugin should include IAppPlugin")
require("IPlayerPlugin" not in playplugin_h and "IPlayerPlugin" not in playplugin_cpp,
        "PlayPlugin should not implement the removed host player interface")
require("public IAppPlugin" in playplugin_h,
        "PlayPlugin should remain a generic app plugin")
require("Q_INTERFACES(IAppPlugin)" in playplugin_h,
        "PlayPlugin should expose only the generic app plugin interface")
require("Q_INTERFACES(IAppPlugin IPlayerPlugin)" not in playplugin_h,
        "PlayPlugin should not expose a player plugin Qt interface")
require("PlaybackContext::instance().setFinder" not in playplugin_cpp,
        "PlayPlugin should not receive host player lookup callbacks")
require("PlayerPluginFinder" not in context_h and "IPlayerPlugin" not in context_h,
        "PlaybackContext should not store host-provided player capabilities")
require("IPlayerPlugin" not in readme and "IPlayerPlugin" not in build_md,
        "docs should describe only IAppPlugin as the plugin contract")
```

- [ ] **Step 4: Run check and verify failure**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: fail because `plugin/IPlayerPlugin.h` still exists and current code still references it.

---

### Task 2: Remove Player Interface From Plugin Contract

**Files:**
- Delete: `plugin/IPlayerPlugin.h`
- Modify: `plugin/IAppPlugin.h`
- Modify: `plugin/CMakeLists.txt`

- [ ] **Step 1: Simplify `IAppPlugin.h`**

Change `plugin/IAppPlugin.h` to remove `<functional>`, `class IPlayerPlugin`, and `PluginContext::PlayerPluginFinder`. The top of the file should become:

```cpp
#pragma once

#include <QString>
#include <QUrl>
#include <QtPlugin>

struct PluginContext
{};
```

Keep `IAppPlugin_IID`, `class IAppPlugin`, and `Q_DECLARE_INTERFACE(IAppPlugin, IAppPlugin_IID)` unchanged.

- [ ] **Step 2: Update `plugin/CMakeLists.txt`**

Change the interface target source list to:

```cmake
add_library(VideoPlayerPlugin INTERFACE
    IAppPlugin.h
)
```

- [ ] **Step 3: Delete `plugin/IPlayerPlugin.h`**

Remove the file:

```bash
git rm plugin/IPlayerPlugin.h
```

- [ ] **Step 4: Run check and verify next failure**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: fail on `PluginManager`, `PlayPlugin`, or `PlaybackContext` still referencing `IPlayerPlugin`.

---

### Task 3: Remove Player Lookup From PluginManager

**Files:**
- Modify: `core/PluginManager.h`
- Modify: `core/PluginManager.cpp`

- [ ] **Step 1: Update `PluginManager.h`**

Remove:

```cpp
#include "IPlayerPlugin.h"
IPlayerPlugin* findPlayerPlugin(const QUrl& url) const;
```

Keep only:

```cpp
#include "IAppPlugin.h"
```

- [ ] **Step 2: Update `PluginManager.cpp` initialization**

Replace the context setup block in `PluginManager::loadPlugin()` with:

```cpp
PluginContext context;

if (!plugin->initialize(context)) {
    LOG_ERROR("PluginManager: plugin {} initialize() failed",
              plugin->name().toStdString());
    loader->unload();
    return false;
}
```

- [ ] **Step 3: Delete `findPlayerPlugin()` implementation**

Remove the full `PluginManager::findPlayerPlugin(const QUrl& url) const` function from `core/PluginManager.cpp`.

- [ ] **Step 4: Run check and verify next failure**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: fail on PlayPlugin or PlaybackContext still referencing `IPlayerPlugin`.

---

### Task 4: Make PlayPlugin App-Only

**Files:**
- Modify: `plugins/PlayPlugin/PlayPlugin.h`
- Modify: `plugins/PlayPlugin/PlayPlugin.cpp`
- Modify: `plugins/PlayPlugin/src/PlaybackContext.h`

- [ ] **Step 1: Update `PlayPlugin.h`**

Remove:

```cpp
#include "IPlayerPlugin.h"
public IPlayerPlugin
Q_INTERFACES(IAppPlugin IPlayerPlugin)
bool canHandle(const QUrl& url) const override;
bool open(const QUrl& url) override;
void play() override;
void pause() override;
void stop() override;
void seek(qint64 positionMs) override;
qint64 duration() const override;
qint64 position() const override;
bool isPlaying() const override;
```

The class declaration should become:

```cpp
class PlayPlugin : public QObject, public IAppPlugin
{
    Q_OBJECT
    Q_INTERFACES(IAppPlugin)
    Q_PLUGIN_METADATA(IID IAppPlugin_IID FILE "PlayPlugin.json")
```

Keep metadata, lifecycle, `hasQmlUI()`, and `qmlComponentUrl()`.

- [ ] **Step 2: Update `PlayPlugin.cpp` lifecycle**

Replace `initialize()` with:

```cpp
bool PlayPlugin::initialize(const PluginContext&)
{
    LOG_INFO("PlayPlugin::initialize()");
    return true;
}
```

Remove these functions from `PlayPlugin.cpp`:

```cpp
bool PlayPlugin::canHandle(const QUrl& url) const;
bool PlayPlugin::open(const QUrl& url);
void PlayPlugin::play();
void PlayPlugin::pause();
void PlayPlugin::stop();
void PlayPlugin::seek(qint64 positionMs);
qint64 PlayPlugin::duration() const;
qint64 PlayPlugin::position() const;
bool PlayPlugin::isPlaying() const;
```

Keep the local `engine()` helper if `shutdown()` still uses it.

- [ ] **Step 3: Update `PlaybackContext.h`**

Remove:

```cpp
#include <functional>
#include "IPlayerPlugin.h"
using PlayerPluginFinder = PluginContext::PlayerPluginFinder;
void setFinder(PlayerPluginFinder finder);
void clearFinder();
bool hasFinder() const;
IPlayerPlugin* findPlugin(const QUrl& url) const;
PlayerPluginFinder m_finder;
```

Keep pending URL and `PlayerEngine*` registration methods.

- [ ] **Step 4: Run check**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: fail only on docs/comments or pass if those are already clean.

---

### Task 5: Update Docs And Comments

**Files:**
- Modify: `README.md`
- Modify: `BUILD.md`
- Modify: `AGENTS.md`
- Modify: `app/CMakeLists.txt`
- Modify: `app/main.cpp`
- Modify: `core/CMakeLists.txt`
- Modify: `plugins/PlayPlugin/CMakeLists.txt`

- [ ] **Step 1: Remove `IPlayerPlugin` from docs**

Update README and BUILD plugin sections so they say:

```markdown
`IAppPlugin` 是宿主加载插件的唯一接口。插件可以提供 QML 页面，也可以在插件内部实现自己的业务能力；宿主不会为播放器、转码器或其他具体领域定义专用插件接口。
```

- [ ] **Step 2: Update CMake and source comments**

Replace comments mentioning `IPlayerPlugin.h`, `IPlayerPlugin 实现`, or `PluginFinder` with generic app plugin wording:

```text
IAppPlugin 接口
宿主插件实现
通用 PluginContext
```

- [ ] **Step 3: Run search**

Run:

```bash
rg -n "IPlayerPlugin|findPlayerPlugin|PlayerPluginFinder|PluginFinder" -g '!build/**'
```

Expected: matches only in historical design/plan docs, or no source/docs matches outside `docs/superpowers/`.

---

### Task 6: Verify, Commit, And Push

**Files:**
- All changed implementation, docs, and test files.

- [ ] **Step 1: Run regression check**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: exit 0.

- [ ] **Step 2: Run build**

Run:

```bash
cmake --build build --parallel
```

Expected: exit 0 and `VideoPlayerApp`, `DummyPlugin`, and `PlayPlugin` targets built.

- [ ] **Step 3: Optional GUI smoke test**

Run:

```bash
./build/app/VideoPlayerApp.app/Contents/MacOS/VideoPlayerApp
```

Expected logs include:

```text
PluginManager: loaded plugin 'DummyPlugin' v1.0.0
PluginManager: loaded plugin 'PlayPlugin' v1.0.0
```

Stop the app after confirming plugin load.

- [ ] **Step 4: Commit**

```bash
git add README.md BUILD.md AGENTS.md app/CMakeLists.txt app/main.cpp core/CMakeLists.txt core/PluginManager.cpp core/PluginManager.h plugin/CMakeLists.txt plugin/IAppPlugin.h plugins/PlayPlugin/CMakeLists.txt plugins/PlayPlugin/PlayPlugin.cpp plugins/PlayPlugin/PlayPlugin.h plugins/PlayPlugin/src/PlaybackContext.h tests/playplugin_regression_checks.py
git add -u plugin/IPlayerPlugin.h
git commit -m "[功能修改] 移除播放器插件接口"
```

- [ ] **Step 5: Push**

```bash
git push
```

Expected: current branch updates on `origin`.
