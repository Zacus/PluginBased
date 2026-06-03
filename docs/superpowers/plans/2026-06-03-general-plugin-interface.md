# General Plugin Interface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the host plugin system load generic application plugins while keeping media playback as an optional plugin capability.

**Architecture:** Add `IAppPlugin` and `PluginContext` as the host-facing plugin contract. Keep `IPlayerPlugin` as a playback-only capability discovered from loaded app plugins with `qobject_cast<IPlayerPlugin*>`.

**Tech Stack:** C++17, Qt 6 `QPluginLoader`, Qt interface macros, CMake, QML, existing text-level Python regression checks.

---

## File Structure

- Create `plugin/IAppPlugin.h`: generic plugin identity, lifecycle, QML page, and card presentation interface.
- Modify `plugin/IPlayerPlugin.h`: remove generic plugin concerns and leave only playback capability methods.
- Modify `core/PluginManager.h` and `core/PluginManager.cpp`: store `IAppPlugin*`, load by `IAppPlugin_IID`, and add `findPlayerPlugin(const QUrl&)`.
- Modify `plugins/PlayPlugin/PlayPlugin.h` and `plugins/PlayPlugin/PlayPlugin.cpp`: implement both `IAppPlugin` and `IPlayerPlugin`; initialize with `PluginContext`.
- Modify `plugins/DummyPlugin/DummyPlugin.h` and `plugins/DummyPlugin/DummyPlugin.cpp`: implement only `IAppPlugin`.
- Modify `plugins/PlayPlugin/src/PlaybackContext.h`: store `PluginContext::PlayerPluginFinder`.
- Modify CMake files only if needed to include the new header in `VideoPlayerPlugin`.
- Modify `tests/playplugin_regression_checks.py`: add structural checks for the new separation.
- Modify `README.md` and `BUILD.md`: update plugin development guidance from player-only plugin to app plugin plus optional player capability.

---

### Task 1: Add Generic Plugin Interface

**Files:**
- Create: `plugin/IAppPlugin.h`
- Modify: `plugin/CMakeLists.txt`
- Test: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: Add failing structural checks**

Append these checks near the existing interface checks in `tests/playplugin_regression_checks.py`:

```python
app_plugin_h = read("plugin/IAppPlugin.h")
iplayer_h = read("plugin/IPlayerPlugin.h")
plugin_cmake = read("plugin/CMakeLists.txt")

require("class IAppPlugin" in app_plugin_h,
        "generic app plugin interface should exist")
require("#define IAppPlugin_IID" in app_plugin_h and
        "com.videoplayer.IAppPlugin/1.0" in app_plugin_h,
        "IAppPlugin should expose the generic plugin IID")
require("struct PluginContext" in app_plugin_h,
        "IAppPlugin should define the minimal host context")
require("using PlayerPluginFinder = std::function<IPlayerPlugin*(const QUrl&)>" in app_plugin_h,
        "PluginContext should expose a player capability finder")
require("virtual QString id() const = 0" in app_plugin_h,
        "IAppPlugin should expose a stable plugin id")
require("virtual bool initialize(const PluginContext& context) = 0" in app_plugin_h,
        "IAppPlugin initialize should receive PluginContext")
require("Q_DECLARE_INTERFACE(IAppPlugin, IAppPlugin_IID)" in app_plugin_h,
        "IAppPlugin should be declared as a Qt plugin interface")
require("IAppPlugin.h" in plugin_cmake,
        "VideoPlayerPlugin interface target should publish IAppPlugin.h")
require("PluginContext" not in iplayer_h,
        "IPlayerPlugin should not own generic host context")
```

- [ ] **Step 2: Run the check and verify it fails**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: fail with `FileNotFoundError` or assertion about `plugin/IAppPlugin.h`.

- [ ] **Step 3: Create `IAppPlugin.h`**

Create `plugin/IAppPlugin.h`:

```cpp
#pragma once

#include <QString>
#include <QUrl>
#include <QtPlugin>
#include <functional>

class IPlayerPlugin;

struct PluginContext
{
    using PlayerPluginFinder = std::function<IPlayerPlugin*(const QUrl&)>;

    PlayerPluginFinder findPlayerPlugin;
};

#define IAppPlugin_IID "com.videoplayer.IAppPlugin/1.0"

class IAppPlugin
{
public:
    virtual ~IAppPlugin() = default;

    virtual QString id()          const = 0;
    virtual QString name()        const = 0;
    virtual QString version()     const = 0;
    virtual QString description() const = 0;

    virtual bool initialize(const PluginContext& context) = 0;
    virtual void shutdown() = 0;

    virtual bool hasQmlUI() const { return false; }
    virtual QUrl qmlComponentUrl() const { return QUrl{}; }
    virtual QString cardIcon() const { return QStringLiteral("⬡"); }
    virtual QString cardName() const { return name(); }
};

Q_DECLARE_INTERFACE(IAppPlugin, IAppPlugin_IID)
```

- [ ] **Step 4: Publish the new header through `VideoPlayerPlugin`**

Update `plugin/CMakeLists.txt` so the interface target includes the new header in its source list:

```cmake
add_library(VideoPlayerPlugin INTERFACE
    IAppPlugin.h
    IPlayerPlugin.h
)
```

Keep the existing include directory and target properties unchanged.

- [ ] **Step 5: Run the check and verify this task passes**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: later tasks may still fail, but the assertions added in this task should pass.

- [ ] **Step 6: Commit**

```bash
git add plugin/IAppPlugin.h plugin/CMakeLists.txt tests/playplugin_regression_checks.py
git commit -m "[功能新增] 新增通用插件接口"
```

---

### Task 2: Convert `IPlayerPlugin` To Playback Capability

**Files:**
- Modify: `plugin/IPlayerPlugin.h`
- Test: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: Add failing checks for capability-only interface**

Append these checks after the Task 1 interface checks:

```python
require("virtual QString name()" not in iplayer_h,
        "IPlayerPlugin should not own generic metadata")
require("virtual QString version()" not in iplayer_h,
        "IPlayerPlugin should not own generic version metadata")
require("virtual QString description()" not in iplayer_h,
        "IPlayerPlugin should not own generic description metadata")
require("virtual bool initialize" not in iplayer_h,
        "IPlayerPlugin should not own generic lifecycle")
require("virtual bool canHandle(const QUrl& url) const = 0" in iplayer_h,
        "IPlayerPlugin should keep media handling capability")
require("virtual bool open(const QUrl& url) = 0" in iplayer_h,
        "IPlayerPlugin should keep open capability")
require("Q_DECLARE_INTERFACE(IPlayerPlugin, IPlayerPlugin_IID)" in iplayer_h,
        "IPlayerPlugin should remain a Qt-discoverable optional capability")
```

- [ ] **Step 2: Run the check and verify it fails**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: fail because `IPlayerPlugin.h` still contains metadata and lifecycle methods.

- [ ] **Step 3: Replace `IPlayerPlugin.h` with capability-only content**

Update `plugin/IPlayerPlugin.h` to:

```cpp
#pragma once

#include <QUrl>
#include <QtPlugin>

#define IPlayerPlugin_IID "com.videoplayer.IPlayerPlugin/1.0"

class IPlayerPlugin
{
public:
    virtual ~IPlayerPlugin() = default;

    virtual bool canHandle(const QUrl& url) const = 0;

    virtual bool open(const QUrl& url) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void seek(qint64 positionMs) = 0;

    virtual qint64 duration() const = 0;
    virtual qint64 position() const = 0;
    virtual bool isPlaying() const = 0;
};

Q_DECLARE_INTERFACE(IPlayerPlugin, IPlayerPlugin_IID)
```

- [ ] **Step 4: Run the interface checks**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: fail in plugin migration or manager checks, not in the capability-only interface checks.

- [ ] **Step 5: Commit**

```bash
git add plugin/IPlayerPlugin.h tests/playplugin_regression_checks.py
git commit -m "[功能修改] 拆分播放器插件能力接口"
```

---

### Task 3: Make `PluginManager` Generic

**Files:**
- Modify: `core/PluginManager.h`
- Modify: `core/PluginManager.cpp`
- Test: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: Add failing manager checks**

Append these checks near the other plugin manager checks:

```python
manager_h = read("core/PluginManager.h")
manager_cpp = read("core/PluginManager.cpp")

require('#include "IAppPlugin.h"' in manager_h,
        "PluginManager should include the generic app plugin interface")
require("IAppPlugin*" in manager_h,
        "PluginManager should store generic app plugins")
require("IPlayerPlugin*                 plugin" not in manager_h,
        "PluginManager should not store IPlayerPlugin as the base plugin")
require("IPlayerPlugin* findPlayerPlugin(const QUrl& url) const" in manager_h,
        "PluginManager should expose player capability lookup")
require("qobject_cast<IAppPlugin*>" in manager_cpp,
        "PluginManager should load generic app plugins")
require("qobject_cast<IPlayerPlugin*>" in manager_cpp,
        "PluginManager should discover optional player capability by cast")
require("PluginContext context" in manager_cpp and
        "context.findPlayerPlugin" in manager_cpp,
        "PluginManager should pass PluginContext into app plugin initialization")
```

- [ ] **Step 2: Run the check and verify it fails**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: fail because `PluginManager` still loads and stores `IPlayerPlugin`.

- [ ] **Step 3: Update `PluginManager.h`**

Change the includes and declarations in `core/PluginManager.h` to this shape:

```cpp
#include "IAppPlugin.h"
#include "IPlayerPlugin.h"
```

Update the public lookup method:

```cpp
IPlayerPlugin* findPlayerPlugin(const QUrl& url) const;
```

Update `PluginEntry`:

```cpp
struct PluginEntry {
    std::unique_ptr<QPluginLoader> loader;
    IAppPlugin*                    plugin = nullptr;
    PluginEntry() = default;
    PluginEntry(PluginEntry&&) = default;
    PluginEntry& operator=(PluginEntry&&) = default;
    PluginEntry(const PluginEntry&) = delete;
    PluginEntry& operator=(const PluginEntry&) = delete;
};
```

- [ ] **Step 4: Update `PluginManager.cpp` loading**

In `PluginManager::loadPlugin()`, replace the cast and initialize block with:

```cpp
auto* plugin = qobject_cast<IAppPlugin*>(obj);
if (!plugin) {
    const QString err = QStringLiteral("%1 does not implement IAppPlugin")
                            .arg(filePath);
    LOG_ERROR("PluginManager: {}", err.toStdString());
    loader->unload();
    emit pluginLoadFailed(filePath, err);
    return false;
}

PluginContext context;
context.findPlayerPlugin = [this](const QUrl& url) -> IPlayerPlugin* {
    return this->findPlayerPlugin(url);
};

if (!plugin->initialize(context)) {
    LOG_ERROR("PluginManager: plugin {} initialize() failed",
              plugin->name().toStdString());
    loader->unload();
    return false;
}
```

Keep the existing log, `PluginEntry`, `pluginsChanged()`, and return flow.

- [ ] **Step 5: Replace `findPlugin()` with `findPlayerPlugin()`**

Replace the old `findPlugin()` implementation with:

```cpp
IPlayerPlugin* PluginManager::findPlayerPlugin(const QUrl& url) const
{
    for (const auto& entry : m_plugins) {
        if (!entry.loader)
            continue;

        QObject* obj = entry.loader->instance();
        auto* player = qobject_cast<IPlayerPlugin*>(obj);
        if (player && player->canHandle(url))
            return player;
    }

    LOG_WARN("PluginManager: no player plugin found for {}",
             url.toString().toStdString());
    return nullptr;
}
```

- [ ] **Step 6: Keep QML-facing accessors generic**

Ensure these methods continue to read from `entry.plugin`:

```cpp
QStringList PluginManager::pluginNames() const;
QString PluginManager::pluginVersion(const QString& name) const;
QString PluginManager::pluginDescription(const QString& name) const;
QString PluginManager::pluginName(int index) const;
QString PluginManager::pluginDescriptionAt(int index) const;
bool PluginManager::pluginHasQmlUI(int index) const;
QUrl PluginManager::pluginQmlUrl(int index) const;
QString PluginManager::pluginCardIcon(int index) const;
QString PluginManager::pluginCardName(int index) const;
```

- [ ] **Step 7: Run the check**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: fail in plugin class migration checks until `PlayPlugin` and `DummyPlugin` are updated.

- [ ] **Step 8: Commit**

```bash
git add core/PluginManager.h core/PluginManager.cpp tests/playplugin_regression_checks.py
git commit -m "[功能修改] 插件管理器改为加载通用插件"
```

---

### Task 4: Migrate `PlayPlugin` To App Plugin Plus Player Capability

**Files:**
- Modify: `plugins/PlayPlugin/PlayPlugin.h`
- Modify: `plugins/PlayPlugin/PlayPlugin.cpp`
- Modify: `plugins/PlayPlugin/src/PlaybackContext.h`
- Test: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: Add failing PlayPlugin checks**

Append these checks near existing PlayPlugin checks:

```python
playplugin_h = read("plugins/PlayPlugin/PlayPlugin.h")
playplugin_cpp = read("plugins/PlayPlugin/PlayPlugin.cpp")
context_h = read("plugins/PlayPlugin/src/PlaybackContext.h")

require("#include \"IAppPlugin.h\"" in playplugin_h,
        "PlayPlugin should include IAppPlugin")
require("public IAppPlugin, public IPlayerPlugin" in playplugin_h,
        "PlayPlugin should implement app plugin and player capability")
require("Q_INTERFACES(IAppPlugin IPlayerPlugin)" in playplugin_h,
        "PlayPlugin should expose both Qt interfaces")
require("Q_PLUGIN_METADATA(IID IAppPlugin_IID FILE \"PlayPlugin.json\")" in playplugin_h,
        "PlayPlugin metadata should use the generic app plugin IID")
require("QString id()          const override" in playplugin_h,
        "PlayPlugin should expose a stable app plugin id")
require("bool initialize(const PluginContext& context) override" in playplugin_h,
        "PlayPlugin should initialize from PluginContext")
require("PlaybackContext::instance().setFinder(context.findPlayerPlugin)" in playplugin_cpp,
        "PlayPlugin should pass the context player finder into PlaybackContext")
require("using PlayerPluginFinder = PluginContext::PlayerPluginFinder" in context_h,
        "PlaybackContext should use PluginContext's player finder type")
```

- [ ] **Step 2: Run the check and verify it fails**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: fail because `PlayPlugin` still implements only `IPlayerPlugin`.

- [ ] **Step 3: Update `PlayPlugin.h`**

Change the includes:

```cpp
#include "IAppPlugin.h"
#include "IPlayerPlugin.h"
```

Change the class declaration and Qt metadata:

```cpp
class PlayPlugin : public QObject, public IAppPlugin, public IPlayerPlugin
{
    Q_OBJECT
    Q_INTERFACES(IAppPlugin IPlayerPlugin)
    Q_PLUGIN_METADATA(IID IAppPlugin_IID FILE "PlayPlugin.json")
```

Add generic app metadata:

```cpp
QString id()          const override { return QStringLiteral("play"); }
QString name()        const override { return QStringLiteral("PlayPlugin"); }
QString version()     const override { return QStringLiteral("1.0.0"); }
QString description() const override { return QStringLiteral("内置播放器：视频 + 播放列表"); }
QString cardIcon()    const override { return QStringLiteral("▶"); }
QString cardName()    const override { return QStringLiteral("视频播放器"); }
```

Change lifecycle declaration:

```cpp
bool initialize(const PluginContext& context) override;
void shutdown() override;
```

Keep all playback capability methods unchanged.

- [ ] **Step 4: Update `PlayPlugin.cpp` lifecycle**

Replace `initialize()` with:

```cpp
bool PlayPlugin::initialize(const PluginContext& context)
{
    LOG_INFO("PlayPlugin::initialize()");
    PlaybackContext::instance().setFinder(context.findPlayerPlugin);
    return true;
}
```

Keep `shutdown()` clearing the finder and stopping the engine.

- [ ] **Step 5: Update `PlaybackContext.h` finder type**

Change includes:

```cpp
#include "IAppPlugin.h"
#include "IPlayerPlugin.h"
```

Change the type alias:

```cpp
using PlayerPluginFinder = PluginContext::PlayerPluginFinder;
```

Change the member:

```cpp
PlayerPluginFinder m_finder;
```

Keep `findPlugin(const QUrl& url) const` returning `IPlayerPlugin*` for compatibility with current internal naming.

- [ ] **Step 6: Run the check**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: fail in `DummyPlugin` migration checks until that plugin is updated.

- [ ] **Step 7: Commit**

```bash
git add plugins/PlayPlugin/PlayPlugin.h plugins/PlayPlugin/PlayPlugin.cpp plugins/PlayPlugin/src/PlaybackContext.h tests/playplugin_regression_checks.py
git commit -m "[功能修改] 播放插件实现通用插件与播放能力"
```

---

### Task 5: Migrate `DummyPlugin` To Generic App Plugin

**Files:**
- Modify: `plugins/DummyPlugin/DummyPlugin.h`
- Modify: `plugins/DummyPlugin/DummyPlugin.cpp`
- Test: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: Add failing DummyPlugin checks**

Append these checks near the existing DummyPlugin checks:

```python
dummy_h = read("plugins/DummyPlugin/DummyPlugin.h")
dummy_cpp = read("plugins/DummyPlugin/DummyPlugin.cpp")

require("#include \"IAppPlugin.h\"" in dummy_h,
        "DummyPlugin should include IAppPlugin")
require("public IAppPlugin" in dummy_h and "public IPlayerPlugin" not in dummy_h,
        "DummyPlugin should be a generic app plugin only")
require("Q_INTERFACES(IAppPlugin)" in dummy_h,
        "DummyPlugin should expose only the generic app plugin interface")
require("Q_PLUGIN_METADATA(IID IAppPlugin_IID FILE \"DummyPlugin.json\")" in dummy_h,
        "DummyPlugin metadata should use generic app plugin IID")
require("bool initialize(const PluginContext& context) override" in dummy_h,
        "DummyPlugin should initialize from PluginContext")
require("canHandle(" not in dummy_h and "open(const QUrl&" not in dummy_h,
        "DummyPlugin should not expose playback capability methods")
require("DummyPlugin::canHandle" not in dummy_cpp and "DummyPlugin::open" not in dummy_cpp,
        "DummyPlugin implementation should not contain playback methods")
```

- [ ] **Step 2: Run the check and verify it fails**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: fail because `DummyPlugin` still implements `IPlayerPlugin`.

- [ ] **Step 3: Update `DummyPlugin.h`**

Change the include:

```cpp
#include "IAppPlugin.h"
```

Change the class declaration:

```cpp
class DummyPlugin : public QObject, public IAppPlugin
{
    Q_OBJECT
    Q_INTERFACES(IAppPlugin)
    Q_PLUGIN_METADATA(IID IAppPlugin_IID FILE "DummyPlugin.json")
```

Use these methods:

```cpp
QString id()          const override { return QStringLiteral("dummy"); }
QString name()        const override { return QStringLiteral("DummyPlugin"); }
QString version()     const override { return QStringLiteral("1.0.0"); }
QString description() const override { return QStringLiteral("Stub plugin for framework validation"); }
QString cardIcon()    const override { return QStringLiteral("⬡"); }
QString cardName()    const override { return QStringLiteral("示例插件"); }

bool initialize(const PluginContext& context) override;
void shutdown() override;

bool hasQmlUI() const override { return true; }
QUrl qmlComponentUrl() const override;
```

Remove playback state members `m_url`, `m_duration`, `m_position`, and `m_playing`.

- [ ] **Step 4: Update `DummyPlugin.cpp`**

Replace lifecycle and remove playback methods:

```cpp
bool DummyPlugin::initialize(const PluginContext&)
{
    LOG_INFO("DummyPlugin::initialize()");
    return true;
}

void DummyPlugin::shutdown()
{
    LOG_INFO("DummyPlugin::shutdown()");
}
```

Keep:

```cpp
QUrl DummyPlugin::qmlComponentUrl() const
{
    return QUrl(QStringLiteral("qrc:/DummyPlugin/qml/DummyPluginView.qml"));
}
```

- [ ] **Step 5: Run the check**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: pass unless documentation or build-related checks are added in later tasks.

- [ ] **Step 6: Commit**

```bash
git add plugins/DummyPlugin/DummyPlugin.h plugins/DummyPlugin/DummyPlugin.cpp tests/playplugin_regression_checks.py
git commit -m "[功能修改] 示例插件改为通用应用插件"
```

---

### Task 6: Update Documentation And Build Verification

**Files:**
- Modify: `README.md`
- Modify: `BUILD.md`
- Modify: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: Add documentation checks**

Append these checks to `tests/playplugin_regression_checks.py`:

```python
readme = read("README.md")
build_md = read("BUILD.md")

require("IAppPlugin" in readme and "IPlayerPlugin" in readme,
        "README should document generic app plugins and optional player capability")
require("通用插件" in readme,
        "README should describe the host plugin model as generic")
require("IAppPlugin" in build_md and "可选播放能力" in build_md,
        "BUILD.md should document app plugin development and optional player capability")
```

- [ ] **Step 2: Run the check and verify it fails**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: fail because docs still describe plugin development as `IPlayerPlugin` only.

- [ ] **Step 3: Update README plugin section**

Replace the README plugin development guidance with text equivalent to:

```markdown
## 开发插件

插件分为两层：

- `IAppPlugin`：所有插件都必须实现的通用应用插件接口，负责元信息、生命周期、主页卡片和可选 QML 页面。
- `IPlayerPlugin`：可选播放能力接口，只有需要处理媒体 URL 并提供播放控制的插件才实现。

普通工具类插件只需继承 `IAppPlugin`，并使用：

```cpp
Q_INTERFACES(IAppPlugin)
Q_PLUGIN_METADATA(IID IAppPlugin_IID FILE "MyPlugin.json")
```

播放器插件同时实现 `IAppPlugin` 和 `IPlayerPlugin`：

```cpp
class MyPlayerPlugin : public QObject, public IAppPlugin, public IPlayerPlugin
{
    Q_OBJECT
    Q_INTERFACES(IAppPlugin IPlayerPlugin)
    Q_PLUGIN_METADATA(IID IAppPlugin_IID FILE "MyPlayerPlugin.json")
};
```
```

- [ ] **Step 4: Update BUILD plugin section**

In `BUILD.md`, update the plugin development example to use `IAppPlugin` as the required base interface and mention `IPlayerPlugin` as optional playback capability. Include this statement:

```markdown
`IPlayerPlugin` 是可选播放能力，不再是宿主加载插件的基础接口。
```

- [ ] **Step 5: Run checks and build**

Run:

```bash
python3 tests/playplugin_regression_checks.py
cmake --build build --parallel
```

Expected: both commands pass.

- [ ] **Step 6: Commit**

```bash
git add README.md BUILD.md tests/playplugin_regression_checks.py
git commit -m "[文档] 更新通用插件开发说明"
```

---

### Task 7: Manual Launch Smoke Test

**Files:**
- No source changes expected.

- [ ] **Step 1: Launch the app**

Run:

```bash
./build/app/VideoPlayerApp.app/Contents/MacOS/VideoPlayerApp
```

Expected:

- App starts without plugin load errors.
- Home panel shows both DummyPlugin and PlayPlugin cards.
- Opening the PlayPlugin card loads `qrc:/PlayPlugin/qml/PlayPluginView.qml`.
- Opening the DummyPlugin card loads `qrc:/DummyPlugin/qml/DummyPluginView.qml`.

- [ ] **Step 2: Check logs if launch fails**

Inspect the app log under:

```text
~/Library/Application Support/MyOrg/VideoPlayer/logs/videoplayer.log
```

Expected for successful plugin loading:

```text
PluginManager: loaded plugin 'DummyPlugin' v1.0.0
PluginManager: loaded plugin 'PlayPlugin' v1.0.0
```

- [ ] **Step 3: Final status**

Run:

```bash
git status --short
```

Expected: only intentionally untracked local media files may remain, such as `6月29日.mov`.
