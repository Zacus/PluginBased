# PlayPlugin Execution Flow

## Current Boundary

PlayPlugin is now an SDK playback-session frontend:

- `PlayerEngine` owns QML-facing state and user commands.
- `PlaybackPipeline` owns Qt/platform objects that cannot live in the Qt-free SDK.
- `SdkPlaybackAdapter` bridges `media_sdk::session::PlaybackSession` events to Qt signals.
- `QtRhiVideoPresenter` presents SDK runtime video frames through the existing `FFmpegSurface`.
- `CoreAudioAudioOutput` is injected into the SDK runtime on macOS.

The SDK owns playback orchestration:

- demux/decode through `media_sdk::Player`
- A/V sync and runtime queues
- audio output timing
- presenter backpressure
- seek generation acceptance
- EOF drain ordering
- native-to-CPU fallback

## Ownership

`PlayerEngine` owns `PlaybackPipeline`.

`PlaybackPipeline` owns:

- `SdkPlaybackAdapter`
- `QtRhiVideoPresenter`
- macOS `CoreAudioAudioOutput`

`FFmpegSurface` is created by QML and observed by `PlaybackPipeline` through `QPointer`.

`SdkPlaybackAdapter` owns exactly one `media_sdk::session::PlaybackSession` for the current open media. Audio output and video presenter are non-owning dependencies passed into the SDK session; their lifetime is controlled by `PlaybackPipeline`.

## Threading

- QML, `PlayerEngine`, `PlaybackPipeline`, and `SdkPlaybackAdapter` live on the Qt object thread.
- SDK decode/runtime workers run inside SDK modules.
- `SdkPlaybackAdapter` queues SDK callbacks back to the Qt object thread and rejects stale callbacks by event serial.
- Rendering completion and presenter backpressure are handled by SDK runtime/presenter callbacks, not by PlayPlugin polling.

## Playback

1. QML calls `PlayerEngine::open(url)`.
2. `PlayerEngine` resets user-visible state and calls `PlaybackPipeline::openFile(url)`.
3. `PlaybackPipeline` creates the SDK runtime chain if a surface is available.
4. `SdkPlaybackAdapter` creates a `PlaybackSession`, opens the media, and starts playback.
5. `PlaybackSession` creates core decode and runtime playback components after media info is available.
6. Runtime frames are delivered to injected audio output and video presenter.

## Seek

1. `PlayerEngine::seek(positionMs)` increments the UI seek generation.
2. `PlaybackPipeline` forwards the command to `SdkPlaybackAdapter`.
3. `PlaybackSession` cancels old frame acceptance, seeks runtime/core, and accepts only the completed generation.
4. `SdkPlaybackAdapter` maps SDK seek completion back to the UI generation and emits `seekCompleted`.

## EOF

SDK core EOF is not exposed directly as final playback completion. The SDK runtime first drains accepted audio/video frames and then reports presented EOF. `SdkPlaybackAdapter` forwards the drained EOF as `endOfFile`, `endOfAudio`, and `endOfVideo` so `PlayerEngine` can complete media once all streams are finished.

## Native Fallback

When native presentation fails, SDK runtime requests CPU fallback. `PlaybackSession` reopens core decode in CPU mode and seeks to the resume position. PlayPlugin only disables the native preference on the adapter and keeps the same Qt presenter surface.

## Diagnostics

SDK runtime diagnostics are surfaced through `ISessionEvents::onRuntimeDiagnostics`. `SdkPlaybackAdapter` logs them as `PlayPerf: sdk ...`.

These diagnostics are observation-only. PlayPlugin must not compute playback policy from them.
