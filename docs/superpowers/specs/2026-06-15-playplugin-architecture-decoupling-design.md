# PlayPlugin Architecture Decoupling Design

## Background

`PlayPlugin` has accumulated several large coordination classes. The most important one is `PlayerEngine`, which currently acts as the QML-facing API, playback state holder, component owner, seek coordinator, surface binder, error recovery point, and media completion state machine. `FFmpegSurface` and `FFmpegDecoder` also contain multiple separable responsibilities, but they should be split after the playback orchestration boundary is cleaner.

The goal is not to add design patterns everywhere. The goal is to isolate responsibilities that change for different reasons while preserving the public QML API and plugin ABI.

## Goals

1. Keep the existing QML API for `PlayerEngine` stable.
2. Move playback component ownership and stop/start coordination out of `PlayerEngine`.
3. Make playback lifecycle state easier to reason about and test.
4. Reduce direct coupling between QML-facing objects and low-level decoder/renderer objects.
5. Preserve Qt thread affinity rules for `QThread`, `QAudioSink`, `QQuickItem`, and Scene Graph resources.
6. Add architecture checks that prevent `PlayerEngine` from regrowing orchestration responsibilities.

## Non-Goals

1. This design does not change the `IAppPlugin` ABI.
2. This design does not replace Qt signals/slots with `std::jthread` or coroutines.
3. This design does not redesign FFmpeg decoding behavior.
4. This design does not change QML filenames, imports, or visible UI.
5. This design does not split `FFmpegSurface` and `FFmpegDecoder` in the first implementation step.

## Recommended Architecture

Use `PlayerEngine` as a thin facade and introduce an internal `PlaybackPipeline` owned by it.

`PlayerEngine` remains the only QML-facing playback object. It keeps properties such as `playbackState`, `position`, `duration`, `volume`, `muted`, `currentMedia`, and `errorString`. It translates QML calls into pipeline commands and translates pipeline signals back into QML notify signals.

`PlaybackPipeline` owns the lower-level playback objects:

- `VideoFrameQueue`
- `AudioFrameQueue`
- `ClockSync`
- `FFmpegDecoder`
- `AudioRenderer`
- `VideoRenderer`

It is responsible for component construction, signal wiring for low-level components, stopping and waiting in a deterministic order, queue abort/reset, seek serial coordination, and native video rendering toggles.

The first phase should keep media state decisions in `PlayerEngine` where doing so minimizes risk. Later phases can move completion flags and state transitions into a small `PlaybackStateMachine`.

## Component Boundaries

### PlayerEngine

Responsibilities:

- Expose stable QML properties, slots, and signals.
- Own `MediaInfo` objects with Qt parent ownership.
- Store QML-visible playback values.
- Bind and clear `FFmpegSurface`.
- Delegate low-level component operations to `PlaybackPipeline`.
- Decide QML-facing playback state transitions during the first phase.

`PlayerEngine` should not directly own `FFmpegDecoder`, `AudioRenderer`, `VideoRenderer`, queues, or `ClockSync` after phase one.

### PlaybackPipeline

Responsibilities:

- Own playback queues, clock, decoder, audio renderer, and video renderer.
- Wire low-level component signals to pipeline signals.
- Provide commands: `openFile`, `play`, `pause`, `stopComponents`, `seek`, `setVolume`, `setMuted`, `setSurface`, and `setNativeVideoRenderingEnabled`.
- Emit decoded media info, position updates, decoder errors, audio/video EOF, decoder EOF, seek completion, and native rendering failure.
- Guarantee shutdown order: decoder stop/wait, audio stop/wait, video stop/reset, queue flush/reset.

Ownership model:

- `PlaybackPipeline` owns components through `std::unique_ptr`.
- Queues and clock are value members owned by `PlaybackPipeline`.
- `FFmpegSurface` remains QML-owned. `PlaybackPipeline` stores it as a `QPointer` observer only.

### Future PlaybackStateMachine

Responsibilities:

- Track `Stopped`, `Playing`, `Paused`, `Finished`, `Seeking`, and `Error` transitions.
- Replace scattered completion flags such as decoder/audio/video/media finished.
- Return explicit transition results that `PlayerEngine` can turn into QML signals.

This is a later step because it touches user-visible playback behavior more deeply than extracting ownership.

### Future FFmpegSurface Rendering Backends

Responsibilities:

- Keep `FFmpegSurface` as a thin `QQuickItem`.
- Move software YUV RHI upload into a software backend.
- Move VideoToolbox native texture handling into a native backend.
- Keep pixel format description in a focused helper module.

Recommended pattern: Strategy plus a small factory selected by frame metadata and runtime graphics API.

### Future FFmpegDecoder Split

Responsibilities:

- Move media open/stream discovery into a `MediaOpener`.
- Move audio/video codec send/receive loops into stream decoder helpers.
- Move hardware frame transfer and pixel normalization into `VideoFrameProcessor`.
- Move performance counters into dedicated logger structs.

Recommended pattern: small RAII helpers and structured result types, not a broad inheritance hierarchy.

## Runtime Flow After Phase One

1. QML calls `PlayerEngine.open(url)`.
2. `PlayerEngine` resets QML-visible state and calls `PlaybackPipeline.openFile(url)`.
3. `PlaybackPipeline` resets queues and clock, applies native rendering policy, and asks `FFmpegDecoder` to open asynchronously.
4. Decoder emits media info to `PlaybackPipeline`.
5. `PlaybackPipeline` forwards media info to `PlayerEngine`.
6. `PlayerEngine` updates QML-visible duration/media info and calls `PlaybackPipeline.startRenderersForMedia(...)`.
7. Renderers emit position and EOF signals through `PlaybackPipeline`.
8. `PlayerEngine` updates QML state and finish handling.

## Error Handling

`PlaybackPipeline` should not hide low-level errors. It forwards decoder and renderer errors to `PlayerEngine`. `PlayerEngine` remains responsible for QML-facing error text and resetting public playback state during phase one.

Shutdown must be idempotent. Calling `stopComponents()` multiple times must be safe because `open()` currently calls `stop()` and destructors also stop components.

## Testing Strategy

Automated checks:

- Add a static architecture check requiring `PlaybackPipeline` files to exist.
- Require `PlayerEngine` to own `std::unique_ptr<PlaybackPipeline>` instead of direct `FFmpegDecoder`, `AudioRenderer`, `VideoRenderer`, queues, or `ClockSync`.
- Require `PlayPlugin` CMake to compile `PlaybackPipeline`.
- Keep existing `playplugin_regression_checks.py` and CTest coverage green.

Manual checks after build:

- Launch the app.
- Open `PlayPlugin`.
- Play, pause, seek, stop, and reopen a media file.
- Confirm video-only and audio-video files still complete correctly.
- Confirm native VideoToolbox fallback still disables native rendering after surface failure.

## Acceptance Criteria

1. A design document and implementation plan exist under `docs/superpowers`.
2. `PlaybackPipeline` owns decoder, renderers, queues, and clock.
3. `PlayerEngine` no longer directly stores those low-level playback members.
4. QML-facing `PlayerEngine` API remains unchanged.
5. Static architecture checks pass.
6. Existing PlayPlugin regression checks pass.
7. CMake build succeeds.
