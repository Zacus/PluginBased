# NV12/P010 Direct Render Design

## Goal

在当前 `da0fd70` 基础上优化 PlayPlugin 播放器，使 CPU 侧 `AV_PIX_FMT_NV12` 和 `AV_PIX_FMT_P010LE` 视频帧不再通过 `sws_scale` 转成 `YUV420P`，而是保持两平面格式上传到 Qt Scene Graph shader 渲染。

## Scope

本阶段只做 CPU 帧两平面直接渲染。硬解帧仍先通过现有 `HardwareDecoderBackend::transferToCpuFrame()` 转到 CPU 可读 `AVFrame`，不接入 VideoToolbox `CVPixelBuffer`、Metal 或平台原生纹理零拷贝。

## Architecture

`FFmpegDecoder` 将 `NV12` 和 `P010LE` 认定为渲染器原生支持格式，避免进入 `normalizeVideoFrame()`。其他不支持格式继续统一转换为 `YUV420P`，保持现有兜底路径。

`FFmpegSurface` 的格式描述从“所有格式都是三平面”扩展为“平面布局 + Y/UV 纹理格式”。三平面格式继续使用 `texY`、`texU`、`texV`；NV12/P010 使用 `texY` 和交错 `texU`，`texV` 只保留占位绑定以兼容现有三 sampler shader。

shader 保持单套 `yuvvideo.frag`。uniform 参数中的第四个分量从简单 `is10bit` 布尔扩展为格式模式：`0=8bit planar`、`1=10bit planar`、`2=8bit semiplanar`、`3=10bit semiplanar`。shader 根据模式决定从 `texU.rg` 读取交错 U/V，或从 `texU.r`、`texV.r` 读取三平面 U/V。

## Color Correctness

NV12/P010 的 U/V 必须从交错 UV 纹理的 `r` 和 `g` 通道读取，不能从占位 `texV` 读取。P010 上传为 `RG16`，但它的 10bit 值位于 16bit 高位，R16/RG16 采样后已经接近 `[0, 1]`，不能复用 planar 10bit 的低位扩展逻辑，否则会明显过亮。现有 planar 10bit 格式仍保留低 10bit 到 `[0, 1]` 的 shader 扩展。

range 继续由 `AVFrame::color_range == AVCOL_RANGE_JPEG` 决定 full range，否则按 limited range。色彩矩阵继续沿用现有策略：明确 `AVCOL_SPC_BT709` 或未声明但宽度大于等于 1280 时使用 BT.709，否则使用 BT.601。此阶段不实现 HDR/PQ/HLG 色调映射。

## Testing

扩展 `tests/playplugin_regression_checks.py`，先验证失败，再实现通过。检查点包括：

- `FFmpegDecoder.cpp` 将 `AV_PIX_FMT_NV12` 和 `AV_PIX_FMT_P010LE` 纳入渲染器支持格式。
- `FFmpegSurface.cpp` 描述 semiplanar 布局，并为 UV 平面使用 `QRhiTexture::RG8` / `QRhiTexture::RG16`。
- `FFmpegSurface.cpp` 对 semiplanar 只上传 Y 与 UV 交错平面，三平面路径仍上传 Y/U/V。
- `yuvvideo.frag` 使用格式模式判断，并从 `texture(texU, vTexCoord).rg` 读取 NV12/P010 的 U/V。
