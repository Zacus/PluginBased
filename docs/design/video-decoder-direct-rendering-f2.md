# Decoder Direct Rendering F2 设计

## 1. 决策背景

F1 证明自管 `get_buffer2` 池在 H.264、HEVC 4K60 和 ProRes 4K120 上均未达到 3%
性能收益门槛，因此性能结论仍为 **NO-GO**。2026-07-14 后续产品决策明确要求实施 F2；
本阶段的目标是提供可控、可诊断的 SDK 自管解码 buffer 生命周期，不把它描述为已验证的
性能优化。

## 2. 范围

- 仅接管 H.264、HEVC、ProRes 软件 decoder，codec 必须声明 `AV_CODEC_CAP_DR1`。
- VideoToolbox 或任何带 `hw_device_ctx` / `hw_frames_ctx` 的 context 不安装 callback。
- 仅接管仍使用 FFmpeg 默认 `get_buffer2` 且 `opaque == nullptr` 的 SDK context。
- `PlayerConfig::enableDecoderBufferPool` 默认开启，PlayPlugin 显式开启；调用方可关闭。
- 不复用转换输出使用的 `CpuVideoPicturePool`，不处理音频 buffer。

## 3. 创建与回退

`Demuxer` 先选择和配置 hardware backend。只有最终选择软件解码时，才在
`avcodec_open2()` 前创建 `DecoderBufferPool` 并安装 callback。硬解打开失败后重新创建的
软件 context 会重新执行同一判断。

每次 callback 再检查 codec、hardware state、frame format/size 和空 buffer 契约。任何
不满足条件、pool 重建失败或 plane 获取失败，都先清理未完整发布的 frame，再调用保存的
`avcodec_default_get_buffer2()`。异常不得越过 C callback 边界。

## 4. Buffer 布局

池按 `(pixel format, width, height)` 建立 format epoch。通过
`avcodec_align_dimensions2()`、`av_image_fill_linesizes()` 和
`av_image_fill_plane_sizes()` 计算每平面布局，每个 plane 使用独立 `AVBufferPool`，并
按 FFmpeg 默认分配器契约预留 `16 + av_cpu_max_align() - 1` 字节。format epoch 变化时
先完整创建新池，再替换旧池。

借出的 `AVBufferRef` 是 frame 数据的实际 owner。池关闭只执行
`av_buffer_pool_uninit()`，不等待在途 frame；最后一个引用释放后旧 pool state 自动销毁。

## 5. 所有权与并发

`OpenedMedia` 负责 codec 与 pool 的成对生命周期，移动赋值和析构均按以下顺序执行：

1. 保持 callback、pool facade 和 hardware backend 有效；
2. 调用 `avcodec_free_context()`，由 FFmpeg 停止并回收复制了 callback/opaque 的 frame
   worker context；
3. 清除 pool 的 attachment 和 active pools，再释放 pool facade 与 hardware backend。

已经打开的多线程 codec 不允许先解绑或销毁 pool。FFmpeg frame worker 会复制
`get_buffer2` 和 `opaque`，只有 codec context 完成释放后才保证不再进入 callback。
内部 API 因此不提供独立 detach；成功 attach 后必须通过成对的 context 释放入口结束生命周期。

`DecodeWorker::diagnostics()` 通过原子 `shared_ptr` 获取只读租约。关闭时先清空对外租约
入口，再按上述顺序释放 codec 和 pool；已经开始的诊断读取可安全读取原子计数，但不能
触发新的 acquire。
callback 使用 mutex 串行保护 format epoch 重建和 plane acquire，底层引用回收由
`AVBufferPool` 自身同步。

## 6. 诊断

SDK event、`PlayerDiagnostics`、playback session runtime snapshot 和 PlayPlugin 日志统一
提供：

- callback、pooled frame、fallback 次数；
- pool rebuild 次数；
- plane acquire 和底层 plane allocation 次数。

stop 后当前媒体的 decoder pool snapshot 归零。统计用于确认路径和定位 fallback，不作为
性能收益证明。

## 7. 验收

- 单元测试覆盖 alignment、复用、并发 acquire、format change、延迟 frame 释放和拒绝
  接管外部 callback/hardware context。
- 生命周期测试覆盖 `OpenedMedia` move、赋值、close 和诊断租约晚于 codec。
- 多线程 codec 销毁测试覆盖 pool/callback 存活到 frame worker 全部退出。
- 真实媒体覆盖 H.264 1080p24、HEVC Main10 4K60、ProRes 422 10-bit 4K120。
- 启用 case 必须全部命中、零 fallback；关闭 case callback 和池统计必须为零。
- 完整 build 和 CTest 必须通过。
