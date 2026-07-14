# Decoder Direct Rendering F2 任务拆分

关联设计：[Decoder Direct Rendering F2 设计](../design/video-decoder-direct-rendering-f2.md)

状态：F2.1 至 F2.5 已完成。F1 的性能 NO-GO 结论保留，F2 是后续明确产品决策。

## F2.1 正式池实现

- 将实验能力收敛为 `media_core` 内部 `DecoderBufferPool`。
- 限制 codec、DR1、默认 callback、空 `opaque` 和 software frame 契约。
- 覆盖 alignment、并发、复用、format epoch、延迟释放与 fallback。
- 提交：`5d2791e [功能新增] 增加正式解码帧缓冲池`

## F2.2 软件解码接入

- 增加 `PlayerConfig::enableDecoderBufferPool`，默认开启。
- 在 software `avcodec_open2()` 前安装，hardware 路径不安装。
- 硬解失败转软件时重新判断；所有失败和 close 路径先解绑再释放 codec。
- 提交：`06e9bd2 [功能修改] 接入软件解码帧缓冲池`

## F2.3 诊断与生命周期

- 贯通 core event、Player diagnostics、session runtime snapshot 和 PlayPlugin 日志。
- 使用原子 `shared_ptr` 诊断租约避免 close 并发 UAF。
- 覆盖 session fallback 仍保持 F2 开启、stop 后 snapshot 回收。
- 提交：`6cad328 [功能修改] 贯通解码帧缓冲池诊断`

## F2.4 生产路径验证

- 扩展 `MediaSdkVideoBenchmark` 的 enabled/disabled 开关和正式池 JSON 统计。
- 运行 H.264、HEVC 4K60、ProRes 4K120，以及 H.264 disabled 对照。
- 完整 build、CTest 和结果文档通过后提交。
- 提交：本任务提交 `[测试] 完成get_buffer2正式路径验证`

## F2.5 审核修复

- codec 释放期间保持 pool/callback 存活，等待 FFmpeg frame worker 全部退出后再清理池。
- 按 FFmpeg 默认分配器契约为每个 plane 预留 CPU 最大对齐所需尾部余量。
- 增加多线程 codec 销毁测试和跨架构 plane 容量断言。
- 提交：本任务提交 `[功能修复] 修正解码帧池销毁与余量契约`
