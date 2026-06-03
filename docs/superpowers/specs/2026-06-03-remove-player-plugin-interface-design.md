# Remove Player Plugin Interface Design

## Goal

Remove `IPlayerPlugin` from the host plugin architecture. The host application should be a generic plugin container and should not know about media playback, URL handling, player controls, or player capability lookup.

After this change, the only plugin contract exposed by the host is `IAppPlugin`.

## Current Problem

The current migration made `IPlayerPlugin` optional, but the host still contains player-specific concepts:

- `PluginContext` exposes `findPlayerPlugin`.
- `PluginManager` includes `IPlayerPlugin.h`.
- `PluginManager` exposes `findPlayerPlugin(const QUrl&)`.
- `PlayPlugin` implements both `IAppPlugin` and `IPlayerPlugin`.
- `PlaybackContext` stores a host-provided player finder.

This is still coupled to the video player domain. A generic plugin manager should not provide a playback service or understand player capabilities.

## Proposed Architecture

Keep `IAppPlugin` as the sole plugin interface:

- `id()`, `name()`, `version()`, `description()`.
- `initialize(const PluginContext& context)`, `shutdown()`.
- Optional QML UI entry and home-card presentation.

Simplify `PluginContext` so it no longer contains playback-specific callbacks. For this migration, it can be an empty struct. It remains useful as a stable extension point for future host services that are genuinely generic.

Delete `IPlayerPlugin.h`.

`PluginManager` only stores `IAppPlugin*` and only exposes generic plugin metadata and QML page accessors. It no longer provides any method that accepts a media URL or returns a player object.

## PlayPlugin Scope

`PlayPlugin` becomes an app plugin only:

```cpp
class PlayPlugin : public QObject, public IAppPlugin
```

Playback behavior stays inside the plugin module:

- `PlayerEngine` remains the playback engine.
- `PlayerView.qml` continues to open files through its own file dialog and playlist model.
- `PlaybackContext` keeps only PlayPlugin-local state, such as the currently registered `PlayerEngine*` and any pending local open URL if still needed.

The host does not call `open()`, `play()`, `pause()`, `stop()`, `seek()`, `duration()`, `position()`, or `isPlaying()` through a plugin interface.

## QML And User Flow

The main application still shows plugin cards. Opening the PlayPlugin card loads `qrc:/PlayPlugin/qml/PlayPluginView.qml`.

All playback actions happen inside the loaded PlayPlugin page. This keeps the current user-visible flow intact while removing host-level player coupling.

## Error Handling

Plugin loading errors remain generic:

- Library does not implement `IAppPlugin`.
- Plugin initialization returns false.
- Plugin QML page fails to load.

There is no host-level "no player plugin found" path after this migration.

## Testing And Verification

Update the regression script to verify:

- `plugin/IPlayerPlugin.h` no longer exists.
- `PluginManager` does not include or mention `IPlayerPlugin`.
- `PluginManager` does not expose `findPlayerPlugin`.
- `PluginContext` does not expose `PlayerPluginFinder` or `findPlayerPlugin`.
- `PlayPlugin` implements only `IAppPlugin`.
- Documentation describes only `IAppPlugin` as the plugin contract.

Verification commands:

```bash
python3 tests/playplugin_regression_checks.py
cmake --build build --parallel
```

Manual smoke test:

```bash
./build/app/VideoPlayerApp.app/Contents/MacOS/VideoPlayerApp
```

Expected runtime behavior:

- DummyPlugin and PlayPlugin both load as generic app plugins.
- PlayPlugin page opens from the home card.
- File opening and playback remain available inside the PlayPlugin page.

## Non-Goals

This does not introduce a new service registry, command bus, media-router API, or cross-plugin playback coordination. If those are needed later, they should be designed as generic host services or plugin-local APIs, not as a player-specific host dependency.
