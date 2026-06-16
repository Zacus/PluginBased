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
7. Keep `FFmpegSurface` and `FFmpegDecoder` as thin facades around focused internal helpers.

## Non-Goals

1. This design does not change the `IAppPlugin` ABI.
2. This design does not replace Qt signals/slots with `std::jthread` or coroutines.
3. This design does not redesign FFmpeg decoding behavior.
4. This design does not change QML filenames, imports, or visible UI.
5. This design did not split `FFmpegSurface` and `FFmpegDecoder` in the first implementation step; later phases now cover those internal boundaries.

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

### FFmpegSurface Rendering Helpers

Current status: partially completed.

`FFmpegSurface` has been reduced toward a `QQuickItem` facade. The following internal helpers now own renderer-specific details:

- `VideoPixelFormat`: maps FFmpeg pixel formats to renderer plane layout, RHI texture format, shader format mode, and 10-bit handling.
- `VideoMaterial`: owns shader, material state, texture wrapper, upload scheduling, and software YUV texture binding.
- `VideoNode`: owns scene graph node behavior, native frame texture lifecycle, and native VideoToolbox texture updates.
- `VideoSurfaceGeometry`: computes the drawn video rectangle for aspect-preserving display.

Remaining responsibility in `FFmpegSurface` should stay limited to QML/scene graph integration: receiving frames, updating item state, and creating/updating the scene node. A future native/software rendering strategy split is still possible, but the immediate pressure is lower because material, node, pixel format, and geometry details are already separated.

### Future FFmpegSurface Rendering Backends

Responsibilities:

- Keep `FFmpegSurface` as a thin `QQuickItem`.
- Move software YUV RHI upload into a software backend.
- Move VideoToolbox native texture handling into a native backend.
- Keep pixel format description in a focused helper module.

Recommended pattern: Strategy plus a small factory selected by frame metadata and runtime graphics API.

### FFmpegDecoder Internal Helpers

Current status: partially completed.

`FFmpegDecoder` remains the `QThread` owner and the boundary that talks to queues and emits playback signals. The following details have been moved out:

- `FFmpegLogBridge`: installs the process-global FFmpeg log callback and adapts FFmpeg C logs to the project logger.
- `MediaOpener`: opens inputs, discovers streams, opens audio/video codec contexts, selects hardware backends, and returns a move-only `OpenedMedia` result.
- `StreamFrameDecoder`: sends packets, receives frames, and normalizes decoded frame PTS to microseconds.
- `VideoFrameProcessor`: decides native direct-render preservation, transfers hardware frames to CPU frames, preserves frame metadata, and normalizes unsupported video pixel formats through swscale.
- `DecodePerformance`: owns decode performance counters, throttled performance logging, and pixel-format formatting helpers.

Ownership and thread model:

- `FFmpegDecoder` owns all helpers as value members.
- `FFmpegDecoder` still owns the active FFmpeg contexts and hardware backend after `MediaOpener` transfers them through `OpenedMedia`.
- Helpers are invoked on the decode thread and do not store `QObject` references.
- Queue pointers remain non-owning and are provided by the playback pipeline.

Remaining `FFmpegDecoder` responsibilities:

- Thread lifecycle: wait for open requests, start/stop decode loop, and wake blocked waits.
- User commands: pause, seek, stop, and VideoToolbox direct-render toggles.
- Decode loop control: `av_read_frame`, EOF behavior, waiting after EOF for stop/new open/seek, and dispatching packets by stream.
- Queue policy: reset/flush/abort queues, push audio frames, push or drop video frames depending on audio-clock availability.
- Signal emission: media info, errors, position updates, EOF, and seek completion.

### Future FFmpegDecoder Split

Responsibilities:

- Move decode-loop command handling into a small `DecoderCommandState` or `DecodeLoopControl` helper.
- Keep `FFmpegDecoder` as the `QThread` facade and signal emitter.
- Consider extracting queue policy only after command/EOF handling is clearer.

Recommended pattern: small value helpers and structured results. Avoid broad inheritance hierarchies and avoid pimpl unless plugin ABI exposure or build-time isolation becomes a real constraint.

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
- Add static architecture checks for each internal helper boundary:
  - `playplugin_surface_decoupling_checks.py`
  - `playplugin_decoder_decoupling_checks.py`
  - `playplugin_video_frame_processor_checks.py`
  - `playplugin_media_opener_checks.py`
  - `playplugin_stream_frame_decoder_checks.py`
  - `playplugin_ffmpeg_log_bridge_checks.py`
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

## Completed Implementation Slices

- `2ab0564 [功能修改] 解耦播放管线协调逻辑`: introduced `PlaybackPipeline` and moved playback component ownership out of `PlayerEngine`.
- `73ce38a [功能修改] 抽离视频像素格式映射`: introduced `VideoPixelFormat`.
- `3d99c4c [功能修改] 抽离视频渲染材质逻辑`: introduced `VideoMaterial`.
- `e2f007e [功能修改] 抽离视频场景节点逻辑`: introduced `VideoNode`.
- `4d5795a [功能修改] 抽离视频显示区域计算`: introduced `VideoSurfaceGeometry`.
- `6a10541 [功能修改] 抽离解码性能统计逻辑`: introduced `DecodePerformance`.
- `8607319 [功能修改] 抽离视频帧处理逻辑`: introduced `VideoFrameProcessor`.
- `194f152 [功能修改] 抽离媒体打开逻辑`: introduced `MediaOpener`.
- `ecfe173 [功能修改] 抽离流帧解码逻辑`: introduced `StreamFrameDecoder`.
- `f866f4f [功能修改] 抽离 FFmpeg 日志桥接`: introduced `FFmpegLogBridge`.
- `eebc0c6 [功能修改] 抽离 EOF 后等待决策`: completed `DecodeLoopControl` seek/EOF waiting boundaries.

## Recommended Next Phase

The next phase should stop broad FFmpegDecoder splitting and focus only on the remaining high-value playback orchestration boundaries. The current priority is to keep QML-facing state decisions and low-level seek side effects out of large facade classes.

Suggested boundaries:

- `PlaybackCompletionTracker` stores decoder/audio/video completion flags and exposes explicit finish decisions for `PlayerEngine`.
- `PlaybackSeekCoordinator` owns the ordered side effects for seek start and seek completion inside `PlaybackPipeline`.

Both helpers should stay non-QObject value/reference helpers. `PlayerEngine` remains responsible for public QML properties and signals, while `PlaybackPipeline` remains the owner of decoder, renderers, queues, and clock.
