# 视频帧分配基线

## 范围

本基线记录对象池接入前的视频帧分配所有权和可重复计数。计数只描述 SDK 明确发起的
分配调用，不把 FFmpeg 解码器内部的 buffer pool 推断成 SDK 分配。

## 当前路径

| 路径 | AVFrame 头 | 像素缓冲 | Presenter |
|---|---|---|---|
| 软件解码直出受支持格式 | `StreamDecoder` 每次 receive 前创建 | FFmpeg decoder 管理 | 每次 present clone 一个帧头 |
| 不受支持格式转换 | decoder 帧头 + `VideoFrameProcessor` 转换帧头 | `av_frame_get_buffer()` 每个转换帧调用一次 | 每次 present clone 一个帧头 |
| 硬件帧转 CPU | decoder/native 帧头 + backend CPU 帧头 | `av_hwframe_transfer_data()` 管理目标 buffer | 每次 present clone 一个帧头 |
| VideoToolbox native 直出 | `StreamDecoder` 创建容器帧头 | CoreVideo/FFmpeg 管理 | 每次 present clone 一个帧头 |

`QtRhiVideoPresenter::makeSurfaceFrame()` 当前对每个成功提交的 SDK frame 调用一次
`av_frame_clone()`。该操作增加帧头和 `AVBufferRef` 引用，不等同于像素 memcpy。

## 可重复计数

`DecodePerformanceStats` 提供：

- `normalizedFrameHeaderAllocations`：转换路径成功创建目标 `AVFrame` 头的次数；
- `normalizedPixelBufferAllocations`：转换路径成功调用 `av_frame_get_buffer()` 的次数。

`MediaSdkCoreVideoFrameProcessorTest` 固定验证：

- YUV420P/NV12 直通路径两个计数均为 0；
- RGB24 转 YUV420P 每帧两个计数均增加 1；
- VideoToolbox 到 NV12 的 transfer 不计入 normalizer 分配。

## 性能测量边界

仓库当前没有固定的 1080p/4K 视频性能样本和稳定的 RSS 基准执行器，因此本任务不提交
机器相关的耗时或 RSS 数字。端到端阶段应使用同一媒体、构建类型、硬件解码配置和播放
时长，记录 decode/normalize latency、RSS、drop count、present copy 以及 pool hit/miss，
再与本计数基线比较。

第一阶段对象池的确定优化点是 `VideoFrameProcessor` 的格式转换目标 buffer。软件解码
直出路径在接入 decoder `get_buffer2` 前不应为命中池而增加整帧复制。
