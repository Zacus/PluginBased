# Decoder Direct Rendering F1 设计

## 1. 背景与证据

F1 验证是否值得用 SDK 管理的像素池替换 FFmpeg 软件解码器的默认
`AVCodecContext::get_buffer2`。当前 4K60 software decode 数据为：

- 5 轮、每轮 600 帧均无 late drop；
- 直接 allocator/buffer 符号 CPU 占比中位数为 `0.054%`；
- 将全部 `libsystem_malloc`、`memset`、`bzero` 计入后的三轮 CPU 上界最大值为
  `4.044%`，低于 5% 门槛。

FFmpeg 8.0.1 的 `avcodec_default_get_buffer2()` 已通过内部 `FramePool` 和每平面
`AVBufferPool` 复用软件解码像素 buffer。格式、宽度或高度变化时创建新池，旧池由
`AVBufferRef` 延迟释放。因此 F1 不是从逐帧分配升级为池化，而是比较 FFmpeg 内建池与
SDK 自管池；预期收益很弱。

## 2. 目标

- 验证公开 `get_buffer2` 契约下的 alignment、frame threading 和 `AVBufferRef` 回收。
- 用同一 runner 对比 default allocator 与 prototype allocator 的帧一致性和性能。
- 验证 format change、pool exhaustion、flush 和在途引用释放的行为。
- 形成明确的 GO/NO-GO，不把实验代码直接演变为正式播放功能。

## 3. 非目标

- 不修改 `media_sdk::PlayerConfig`、公共 API 或 ABI。
- 不接入 PlayPlugin、`DecodeWorker` 或默认 SDK 播放路径。
- 不处理音频 buffer。
- 不替换 VideoToolbox 或其他硬件帧分配器。
- 不复用 `CpuVideoPicturePool`。它拥有完整转换输出 `AVFrame`，而本原型向 decoder
  提供的是满足 codec alignment 的 `AVBufferRef` 平面。
- 不复制 FFmpeg `get_buffer.c` 私有实现或依赖 `AVCodecContext::internal`。

## 4. 实验结构

```text
MediaSdkGetBuffer2Benchmark
  -> FFmpeg demux + software decoder
  -> allocator mode
       default   -> avcodec_default_get_buffer2
       prototype -> ExperimentalDecoderBufferPool
                      -> public avcodec_align_dimensions2
                      -> public av_image_fill_* helpers
                      -> public AVBufferPool per plane
                      -> fallback to avcodec_default_get_buffer2
  -> frame checksum + timing + allocation diagnostics
```

实验 runner 独立放在 `tools/video_benchmark/`。它不链接或修改 SDK 播放控制器，确保
原型删除后产品行为不变。

## 5. 所有权与生命周期

| 资源 | Owner | 生命周期和释放 |
|---|---|---|
| `AVFormatContext` | runner | decode/flush 完成后释放 |
| `AVCodecContext` | runner | 在 pool facade 之前释放，停止新的 callback |
| `ExperimentalDecoderBufferPool` | runner | 通过 `AVCodecContext::opaque` 被 callback 借用 |
| 每平面 `AVBufferPool` | prototype pool | format key 变化或 close 时 `av_buffer_pool_uninit` |
| 借出的 `AVBufferRef` | decoder/output frame | 最后一个引用释放后由 FFmpeg pool callback 回收 |

`AVCodecContext::opaque` 是非拥有指针，只在 `avcodec_open2()` 到
`avcodec_free_context()` 之间有效。runner 必须先停止/释放 codec context，再销毁 pool
facade。`av_buffer_pool_uninit()` 不等待在途引用；旧 pool state 在最后一个
`AVBufferRef` 释放后销毁。

## 6. 线程与同步

FFmpeg 文档允许 frame threading worker 调用 `get_buffer2`，但同一 context 不会并发进入
该 callback。原型仍使用 mutex 保护 format key、pool 重建和计数，避免把该前提扩散到
回收路径或未来测试夹具。

借出和归还由 `AVBufferPool` 管理；自定义 allocation callback 只记录真正创建的底层
buffer 数量，不持有 frame、PTS、generation 或 codec 状态。

## 7. 分配契约

prototype callback 仅在以下条件全部成立时提供自管 buffer：

- `codec_type == AVMEDIA_TYPE_VIDEO`；
- codec 声明 `AV_CODEC_CAP_DR1`；
- `hw_frames_ctx == nullptr`；
- frame 的 format、width、height 有效；
- `avcodec_align_dimensions2()`、`av_image_fill_linesizes()` 和
  `av_image_fill_plane_sizes()` 全部成功；
- 不超过 `AV_NUM_DATA_POINTERS` 支持范围。

每个 plane 的 data、linesize 和 `AVBufferRef` 必须满足目标 CPU alignment，并预留
FFmpeg 要求的边缘 padding。任一条件不满足或 pool 获取失败时调用
`avcodec_default_get_buffer2()`；不得部分发布 prototype buffer。

## 8. 格式变化与关闭

format、原始 width 或 height 任一变化即切换 format epoch：

1. 创建一组新 `AVBufferPool`；
2. 成功后替换 active pools；
3. 对旧 pools 调用 `av_buffer_pool_uninit()`；
4. 旧在途 frame 继续持有旧 pool 的 `AVBufferRef`，释放时不会进入新 epoch。

close 后不再允许 callback。runner 的销毁顺序保证不会出现 close 后新 acquire；测试必须
覆盖 format change 后同时持有新旧 frame，以及 codec context 释放后再释放输出 frame。

## 9. 诊断

prototype runner 至少输出：

- callback、prototype、fallback 次数；
- pool rebuild 次数；
- 底层 plane buffer allocation 次数；
- frame 数、checksum、wall/user/system CPU 和 max RSS；
- codec、format、width、height、thread count。

default 模式同样记录 callback 次数，但不伪造 FFmpeg 私有池的 allocation 数量。

## 10. 验证与决策

功能验证：

- H.264 1080p24、HEVC Main10 4K60，以及 synthetic ProRes 422 10-bit 4K120 压力项；
- default/prototype 帧数与 checksum 一致；
- frame threading、flush、重复 open/close；
- format change、新旧 buffer 同时在途；
- 不支持格式和非 DR1 codec 回退默认 allocator；
- hardware context 必须回退默认 allocator。

性能实验采用 Release、固定媒体、1 次预热加 5 次正式运行。只有同时满足以下条件才
建议进入 F2：

- 不增加整帧复制，功能和生命周期测试全部通过；
- wall 或 CPU 中位数改善至少 3%，或 allocation/延迟抖动有可重复显著改善；
- RSS 不增加超过 5%，无新增丢帧；
- prototype 没有依赖 codec 私有实现，所有不兼容路径可无行为差异地 fallback。

收益不足或复杂度无法由公共契约覆盖时结论为 NO-GO，保留报告并删除或封存原型，不进入
F2。

## 11. 实验结果

2026-07-14 完成 H.264 1080p24、HEVC Main10 4K60 和 synthetic ProRes 422 10-bit
4K120 的 Release 对照，各 1 次预热加 5 次正式运行：

- H.264：wall 回退 `0.47%`，CPU 改善 `0.53%`，RSS 降低 `0.26%`；
- HEVC 4K60：wall 回退 `0.50%`，CPU 回退 `0.29%`，RSS 增加 `0.02%`；
- ProRes 4K120：default/prototype 吞吐 `267.14/269.95 fps`，wall 改善 `1.04%`，
  CPU 回退 `0.15%`，RSS 增加 `0.09%`；
- 两种 allocator 的目标帧数和 checksum 全部一致；
- prototype 全部命中、零 fallback，H.264 以 45 次 plane allocation 服务 750 次 acquire，
  HEVC 以 51/1830 次、ProRes 以 36/360 次 allocation/acquire 完成复用。

生命周期可行性成立，但产品主 case 没有收益，极限压力项最佳收益也仅 `1.04%`，低于
3% 门槛，且 FFmpeg default 本身已经池化。F1 结论为 **NO-GO**，当时不启动 F2，不向
`PlayerConfig` 或 PlayPlugin 引入实验开关。完整结果见
`docs/performance/video-decoder-direct-rendering-f1-results.md`。

## 12. 后续产品决策

F1 的性能 NO-GO 结论不变。2026-07-14 后续明确要求实施 F2，因此正式实现以可控生命周期、
诊断和显式开关为目标启动，不宣称性能收益。正式设计见
`docs/design/video-decoder-direct-rendering-f2.md`。
