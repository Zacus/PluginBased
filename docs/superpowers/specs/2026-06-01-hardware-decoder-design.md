# Hardware Decoder Design

> Status: approved design for planning. Scope is PlayPlugin video decoding only.

## Goal

Add a cross-platform hardware decoding architecture for `plugins/PlayPlugin` while making the first working backend macOS VideoToolbox. The first phase should improve iPhone HEVC/HDR `.mov` playback without rewriting the existing QML UI, frame queues, video renderer, or QRhi shader surface.

## Current Context

`FFmpegDecoder` currently opens media streams, owns the FFmpeg codec contexts, decodes packets, normalizes video frames, and pushes CPU `AVFrame` objects into `VideoFrameQueue`. `VideoRenderer` consumes those frames on the GUI thread and sends them to `FFmpegSurface`, which uploads Y/U/V planes to QRhi textures.

High-resolution iPhone HEVC/HDR samples can fall behind the audio clock under software decoding. The current synchronization layer can drop late frames or periodically force a frame to avoid black/frozen output, but it cannot make software HEVC decoding fast enough.

## Chosen Approach

Use a cross-platform hardware decoder abstraction with platform-specific backends:

- macOS: VideoToolbox backend implemented in phase 1.
- Windows: D3D11VA backend skeleton in phase 1; it reports unavailable until a separate Windows implementation is designed and built.
- Linux: VAAPI backend skeleton in phase 1; it reports unavailable until a separate Linux implementation is designed and built.

All phase-1 hardware decoded frames are transferred back to CPU `AVFrame` objects using FFmpeg hardware-frame transfer APIs. The existing `FrameQueue`, `VideoRenderer`, and `FFmpegSurface` paths continue to receive CPU frames and remain responsible for timing and rendering.

## Non-Goals

- No zero-copy native texture rendering in phase 1.
- No QRhi/Metal/D3D/VAAPI texture interop in phase 1.
- No complete Windows or Linux hardware decoding implementation in phase 1.
- No UI redesign or playback-control behavior change.
- No packaging redesign beyond linking any required FFmpeg hardware APIs already present in the current FFmpeg libraries.

## Proposed Files

- `plugins/PlayPlugin/src/hw/HardwareDecoderBackend.h`
  Defines the backend interface used by `FFmpegDecoder`.

- `plugins/PlayPlugin/src/hw/HardwareDecoderFactory.h`
  Declares backend selection helpers.

- `plugins/PlayPlugin/src/hw/HardwareDecoderFactory.cpp`
  Chooses a backend for a codec and platform, returning no backend when hardware decoding is unavailable.

- `plugins/PlayPlugin/src/hw/VideoToolboxBackend.h`
  Declares the macOS VideoToolbox backend.

- `plugins/PlayPlugin/src/hw/VideoToolboxBackend.cpp`
  Creates the FFmpeg VideoToolbox device/context, configures the decoder, and transfers hardware frames to CPU frames.

- `plugins/PlayPlugin/src/hw/D3D11VABackend.h`
  Windows backend skeleton.

- `plugins/PlayPlugin/src/hw/D3D11VABackend.cpp`
  Returns unavailable in phase 1.

- `plugins/PlayPlugin/src/hw/VaapiBackend.h`
  Linux backend skeleton.

- `plugins/PlayPlugin/src/hw/VaapiBackend.cpp`
  Returns unavailable in phase 1.

- `plugins/PlayPlugin/src/FFmpegDecoder.h`
  Stores the selected backend and any hardware decode state needed by the decoder.

- `plugins/PlayPlugin/src/FFmpegDecoder.cpp`
  Uses the factory during video codec setup, configures `AVCodecContext` for hardware decoding when available, and transfers hardware frames to CPU frames before normalization and queueing.

- `plugins/PlayPlugin/CMakeLists.txt`
  Adds the new backend source files to the `PlayPlugin` target.

- `tests/playplugin_regression_checks.py`
  Adds structural regression checks for backend abstraction, VideoToolbox selection, fallback behavior, and no direct platform branching in the main decoder path beyond factory usage.

## Backend Interface

The backend interface should expose a small set of operations:

- `name()`
  Returns a stable backend name for logs, such as `videotoolbox`.

- `isAvailableForCodec(const AVCodec* codec, AVCodecID codecId)`
  Reports whether the backend should be tried for the selected video codec.

- `configureContext(AVCodecContext* codecContext)`
  Creates/attaches FFmpeg hardware context state before `avcodec_open2()`.

- `isHardwareFrame(const AVFrame* frame)`
  Reports whether a decoded frame needs transfer.

- `transferToCpuFrame(const AVFrame* frame)`
  Returns a CPU-readable `AVFramePtr` suitable for the existing renderer path.

- `reset()`
  Releases hardware state during close or re-open.

The interface should be FFmpeg-centric and not expose Qt or QML types.

## Decoder Flow

When opening the video stream:

1. Find the FFmpeg decoder as today.
2. Ask `HardwareDecoderFactory` for the best available backend.
3. If a backend exists, call `configureContext()` before `avcodec_open2()`.
4. If hardware setup or `avcodec_open2()` fails, log the failure, release the backend, recreate a clean `AVCodecContext`, and reopen with software decoding.
5. Continue playback even when hardware decoding is unavailable.

When receiving decoded frames:

1. If the frame is hardware-backed, call `transferToCpuFrame()`.
2. If transfer succeeds, normalize the transferred CPU frame.
3. If transfer fails for a frame, drop that frame and log a throttled warning.
4. If the frame is already CPU-readable, use the existing path.

## Pixel Format Expectations

The first phase should continue to rely on existing renderer-supported CPU formats:

- `AV_PIX_FMT_YUV420P`
- `AV_PIX_FMT_YUVJ420P`
- `AV_PIX_FMT_YUV420P10LE`
- `AV_PIX_FMT_YUV422P10LE`
- `AV_PIX_FMT_YUV444P10LE`

Unsupported transferred CPU formats should continue through `normalizeVideoFrame()`, which converts to `AV_PIX_FMT_YUV420P` using swscale.

## Fallback Rules

Hardware decoding must be opportunistic:

- If no backend supports the codec, use software decoding.
- If hardware device creation fails, use software decoding.
- If hardware codec opening fails, rebuild the codec context and use software decoding.
- If a hardware frame transfer fails for a single frame, drop that frame.
- Repeated transfer failures may disable the backend for the current media only, but phase 1 may keep this simple and rely on frame dropping plus logs.

Playback must not fail solely because hardware decoding is unavailable.

## Logging

Add concise logs at state transitions:

- Selected backend name.
- Hardware setup success or failure.
- Fallback to software decoding.
- Hardware frame transfer failures, throttled so they do not spam per frame.

Do not add per-frame debug logs in the render loop.

## Testing

Use the existing static regression script for structural checks:

- Factory and backend files exist.
- `FFmpegDecoder` asks the factory for hardware decoding.
- `VideoToolboxBackend` is compiled only for Apple platforms.
- Windows/Linux skeletons do not activate hardware decoding in phase 1.
- Hardware setup failure explicitly falls back to software decoding.
- Hardware frames are transferred before `normalizeVideoFrame()`.

Manual validation should include:

- Existing non-HEVC files still play.
- The sample `6月29日.mov` opens and advances frames.
- Logs show VideoToolbox selected on macOS when supported.
- If hardware setup fails, logs show fallback and playback still starts.

## Risks

- VideoToolbox support depends on how the local FFmpeg build was configured.
- `av_hwframe_transfer_data()` may still be too expensive for some 4K/60 clips, though it should be significantly better than full software HEVC decoding.
- HDR/HLG color accuracy remains limited by the existing YUV shader path and is not solved by phase 1.
- Windows/Linux skeletons must be clearly inactive to avoid giving a false impression of support.

## Follow-Up Phase

If phase 1 improves decode but still cannot make 4K/60 smooth enough, the next phase should investigate native hardware frame rendering:

- macOS: VideoToolbox `CVPixelBuffer` to Metal/QRhi texture.
- Windows: D3D11 texture interop.
- Linux: VAAPI/Vulkan/DMABUF path.

That second phase should be designed separately because it changes `FFmpegSurface` and renderer ownership boundaries.
