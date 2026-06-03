# General Plugin Interface Design

## Goal

Refactor the plugin system so the host application manages general application plugins, not only player plugins. Media playback remains available as an optional capability implemented by plugins that need it.

The immediate motivation is to stop forcing every plugin to implement player-only methods such as `open()`, `play()`, `pause()`, `seek()`, and `canHandle()`. Future plugins should be able to provide settings pages, tools, media utilities, themes, diagnostics, or other application features without pretending to be media players.

## Current State

`PluginManager` currently loads only objects that implement `IPlayerPlugin`. The interface combines three concerns:

- Generic plugin metadata and lifecycle.
- QML page/card integration for the home panel.
- Player-specific media handling and playback control.

Because `PluginManager` stores `IPlayerPlugin*`, the host-level plugin registry is tied to playback behavior. `DummyPlugin` and `PlayPlugin` must both implement the full player interface even when the plugin is only acting as an app card or framework example.

## Proposed Architecture

Introduce a new base interface named `IAppPlugin` in the `plugin/` module. This becomes the only interface required by `PluginManager`.

`IAppPlugin` owns general plugin concerns:

- Stable identity: `id()`, `name()`, `version()`, `description()`.
- Lifecycle: `initialize(const PluginContext& context)`, `shutdown()`.
- Optional UI entry: `hasQmlUI()`, `qmlComponentUrl()`.
- Home panel presentation: `cardIcon()`, `cardName()`.

`PluginManager` changes from storing `IPlayerPlugin*` to storing `IAppPlugin*`. Existing QML APIs such as `pluginCount`, `pluginNames`, `pluginCardName(index)`, `pluginQmlUrl(index)`, and `pluginHasQmlUI(index)` continue to work, but they read from the generic interface.

## Player Capability

Keep `IPlayerPlugin`, but turn it into an optional capability interface instead of the base plugin contract.

`IPlayerPlugin` owns only playback-specific behavior:

- `canHandle(const QUrl& url)`.
- `open(const QUrl& url)`.
- `play()`, `pause()`, `stop()`, `seek(qint64 positionMs)`.
- `duration()`, `position()`, `isPlaying()`.

`PlayPlugin` implements both interfaces:

```cpp
class PlayPlugin : public QObject, public IAppPlugin, public IPlayerPlugin
```

Generic plugins implement only `IAppPlugin`.

The Qt plugin metadata IID should target the base plugin interface, for example `com.videoplayer.IAppPlugin/1.0`, so `QPluginLoader` can load all app plugins through the same host path. The optional player capability is discovered with `qobject_cast<IPlayerPlugin*>`.

## Plugin Context

Add a small `PluginContext` type in the `plugin/` module. For the first migration it should stay minimal and avoid pulling host internals into plugin interfaces.

The initial context provides a callback for player-capability discovery:

```cpp
using PlayerPluginFinder = std::function<IPlayerPlugin*(const QUrl&)>;
```

This preserves the current `PlaybackContext` behavior without making `IAppPlugin` depend on `PluginManager`. More host services can be added later when there is a concrete use case.

## Capability Lookup

`PluginManager` provides a player-specific lookup method:

```cpp
IPlayerPlugin* findPlayerPlugin(const QUrl& url) const;
```

It iterates over loaded `IAppPlugin` entries, casts each plugin QObject to `IPlayerPlugin*`, and returns the first player plugin whose `canHandle(url)` returns true.

This keeps capability logic inside the manager while keeping the manager's storage and QML-facing APIs generic.

## Existing Plugin Migration

`PlayPlugin`:

- Implements `IAppPlugin` for metadata, lifecycle, QML page, and home card presentation.
- Implements `IPlayerPlugin` for playback behavior.
- Receives `PluginContext` in `initialize()` and passes the context's player finder into `PlaybackContext`.

`DummyPlugin`:

- Becomes a generic example plugin implementing `IAppPlugin`.
- Keeps its QML page for validating app-card loading.
- Drops playback-only state and methods unless a dedicated dummy player capability is still needed later.

## QML Impact

The home panel remains an app/plugin launcher. Most QML API names can stay stable in this migration to limit blast radius.

Recommended text changes:

- Treat cards as application plugins, not player plugins.
- Keep `PluginManager.pluginCount` and index-based card accessors.
- Keep plugin pages loaded with `pluginQmlUrl(index)`.

No new routing model is required.

## Error Handling

`PluginManager::loadPlugin()` should reject libraries that do not implement `IAppPlugin` and emit `pluginLoadFailed(path, reason)`.

Player lookup should return `nullptr` when no loaded plugin implements `IPlayerPlugin` for a URL. That should be logged as a playback capability miss, not as a generic plugin loading failure.

Plugin initialization failure should unload that library and leave the remaining plugins active.

## Testing And Verification

Add or update the existing text-level regression script to verify:

- `PluginManager` includes and stores `IAppPlugin`, not `IPlayerPlugin`.
- `IPlayerPlugin` remains present as an optional playback capability.
- `PlayPlugin` implements both `IAppPlugin` and `IPlayerPlugin`.
- `DummyPlugin` implements `IAppPlugin` only.
- `PluginManager` exposes `findPlayerPlugin()` or equivalent player-capability lookup.
- Existing QML plugin card APIs still exist.

Verification after implementation:

```bash
python3 tests/playplugin_regression_checks.py
cmake --build build --parallel
```

Manual launch remains recommended because this is a plugin loading and QML routing change:

```bash
./build/app/VideoPlayerApp.app/Contents/MacOS/VideoPlayerApp
```

## Non-Goals

This migration does not introduce a plugin marketplace, dynamic install/uninstall UI, dependency resolution between plugins, version negotiation, or a general service locator. It only separates the base plugin contract from player-specific capability.

This migration also does not redesign the player UI or playback engine.
