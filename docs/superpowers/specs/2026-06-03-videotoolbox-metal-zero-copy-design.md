# VideoToolbox Metal Zero-Copy Design

## Goal

在 macOS + Metal + VideoToolbox 下，让 `AV_PIX_FMT_VIDEOTOOLBOX` 硬解帧通过 `CVPixelBufferRef -> CVMetalTextureRef -> id<MTLTexture> -> QRhiTexture` 直接进入当前 QML Scene Graph shader 渲染，避免 `av_hwframe_transfer_data()` 带来的 CPU 拷贝和格式转换成本。

## Scope

本阶段只支持 Apple 平台的 VideoToolbox + Metal。非 Apple 平台、Qt 非 Metal RHI 后端、VideoToolbox 输出格式不受支持、native texture 创建失败时，继续走现有 CPU fallback：`av_hwframe_transfer_data()` 转为 CPU NV12/P010，再走已有 `FFmpegSurface` 上传路径。

不在本阶段实现 D3D11VA、VAAPI、HDR 色调映射、跨平台 native texture 抽象，也不替换现有 CPU NV12/P010 直接渲染路径。

## Architecture

新增 Apple-only native texture bridge，建议文件：

- `plugins/PlayPlugin/src/native/NativeVideoFrame.h`
- `plugins/PlayPlugin/src/native/AppleMetalVideoTextureBridge.h`
- `plugins/PlayPlugin/src/native/AppleMetalVideoTextureBridge.mm`

`FFmpegDecoder` 负责保留或转换硬解帧。`FFmpegSurface` 确认当前 Qt Scene Graph 使用 Metal 后，`PlayerEngine` 才启用 decoder 的 VideoToolbox direct render 开关。开关启用时，`AV_PIX_FMT_VIDEOTOOLBOX` 帧直接入队；开关关闭时继续调用 `transferHardwareFrameToCpu()`。decoder 不接触 Metal、CoreVideo texture cache 或 Qt RHI native texture 细节。

`FFmpegSurface` 负责渲染线程资源绑定。它判断帧是 CPU frame 还是 VideoToolbox native frame。CPU frame 走现有 QRhi 上传路径；native frame 通过 `AppleMetalVideoTextureBridge` 创建 Y/UV 两个 native QRhiTexture，并绑定到现有 `yuvvideo.frag` 的 `texY` 和 `texU`，`texV` 继续使用占位纹理。

## Data Flow

Native zero-copy path:

```text
VideoToolbox decoder
  -> AVFrame(format=AV_PIX_FMT_VIDEOTOOLBOX)
  -> VideoFrameQueue
  -> VideoRenderer::frameReady()
  -> VideoFrameData holding AVFramePtr
  -> FFmpegSurface::updatePaintNode()
  -> AppleMetalVideoTextureBridge::createTextures()
  -> CVMetalTextureCacheCreateTextureFromImage()
  -> QRhiTexture::createFrom()
  -> yuvvideo.frag
```

Fallback path:

```text
VideoToolbox decoder
  -> AVFrame(format=AV_PIX_FMT_VIDEOTOOLBOX)
  -> av_hwframe_transfer_data()
  -> CPU NV12/P010 AVFrame
  -> existing FFmpegSurface CPU texture upload
  -> yuvvideo.frag
```

## Lifetime

`VideoFrameData` must keep the original `AVFramePtr` alive while native textures are bound, because FFmpeg owns the `CVPixelBufferRef` through the VideoToolbox frame. `VideoMaterial::currentFrame` continues to retain the latest rendered frame until the next frame replaces it.

`CVMetalTextureRef` objects must be retained inside a per-frame native texture bundle held by `VideoMaterial`. `QRhiTexture` objects created by `createFrom()` wrap external `id<MTLTexture>` handles and do not own the underlying CoreVideo texture. The native texture bundle is released only after the next frame has replaced the current frame state.

## Format Mapping

Supported `CVPixelBuffer` formats:

- `kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange` (`420v`)
- `kCVPixelFormatType_420YpCbCr8BiPlanarFullRange` (`420f`)
- `kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange` (`x420`)
- `kCVPixelFormatType_420YpCbCr10BiPlanarFullRange` (`xf20`)

Metal plane mapping:

- 8-bit Y plane: `MTLPixelFormatR8Unorm`
- 8-bit UV plane: `MTLPixelFormatRG8Unorm`
- 10-bit Y plane: `MTLPixelFormatR16Unorm`
- 10-bit UV plane: `MTLPixelFormatRG16Unorm`

Shader mode:

- `420v/420f` use `formatMode=2` (8-bit semiplanar)
- `x420/xf20` use `formatMode=3` (10-bit semiplanar, high-bit aligned, no low-10bit expansion)

Range:

- `420v/x420` are limited range
- `420f/xf20` are full range

Color matrix:

- Use `AVFrame::colorspace == AVCOL_SPC_BT709` when declared.
- If colorspace is unspecified and width is at least 1280, use BT.709.
- Otherwise use BT.601.

## Fallback and Logging

Native render is attempted only when all conditions are true:

- Build target is Apple.
- Qt scene graph uses Metal RHI.
- Frame format is `AV_PIX_FMT_VIDEOTOOLBOX`.
- A `CVPixelBufferRef` is available.
- Pixel buffer format is one of the supported bi-planar formats.
- `CVMetalTextureCacheCreateTextureFromImage()` succeeds for both planes.
- `QRhiTexture::createFrom()` succeeds for both planes.

Fallback 分两层：

- 启动/打开文件前，如果 `FFmpegSurface` 不能确认 Metal RHI 可用，`PlayerEngine` 不启用 decoder direct render，所有 VideoToolbox 帧继续走现有 CPU transfer。
- 播放中如果 native texture 创建连续失败，`FFmpegSurface` 发出失败信号，`PlayerEngine` 关闭 decoder direct render。已经到达 Surface 的当前 native 帧无法在渲染线程安全地转 CPU，因此只丢弃并计数；后续新解码帧回到 CPU transfer 路径。

Add throttled performance counters so logs show native attempts, native successes, native failures, CPU transfers, dropped native frames, and fallback frames.

## Verification

Verification must include:

- Static regression checks for Apple native bridge symbols, direct-frame decoder path, fallback path, and P010 high-bit handling.
- `python3 tests/playplugin_regression_checks.py`
- `cmake --build build --parallel`
- Manual playback on macOS Metal with H.264/HEVC content and log confirmation that native frame counters increase while hardware transfer counters decrease.
