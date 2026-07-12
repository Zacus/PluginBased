# 视频帧对象池真实媒体 Benchmark 结果

## F1 门禁结论

**CLOSED：不启动 Decoder Direct Rendering F1。**

门禁原因：

- 未提供 decoder allocator 的 Allocations/Time Profiler 归因报告

门禁要求每个必选媒体至少完成清单指定的轮数、帧数和 checksum 稳定、释放后
`inFlight=0`，并提供 allocator 归因数据。仅有 wall time/RSS 对比不能证明
FFmpeg decoder allocator 是瓶颈。

## 对比结果

| Case | Runs | Wall baseline/current | Delta | CPU baseline/current | Delta | RSS baseline/current | Delta | Pool acquire |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| h264_1080p24 | 5 | 251.78/255.57 ms | +1.50% | 1476.79/1477.88 ms | +0.07% | 103.50/103.55 MiB | +0.05% | 0 |
| hevc_4k24 | 5 | 1007.79/1005.35 ms | -0.24% | 6878.34/6915.70 ms | +0.54% | 430.89/431.05 MiB | +0.04% | 0 |

两个必选 case 均为 renderer 可直接消费的 `yuv420p`，current 的 pool acquire 为 0。
因此这组结果专门观察 software decoder direct-output 路径；当前对象池按设计不应改变
该路径。wall/CPU/RSS 的中位数变化均不足以证明 decoder allocator 是瓶颈。

## 测量范围

runner 直接驱动 `media_sdk::Player`，使用真实 MP4 码流执行 software decode，并由
无 UI frame sink 保留 3 帧模拟下游持有。它测量 core decode、frame publication 和
进程资源使用，不包含 Qt Scene Graph、GPU present、音频设备或实时播放节流。

| Case | Codec | Source | SHA-256 |
|---|---|---|---|
| h264_1080p24 | h264 | [official test media](https://test.playready.microsoft.com/media/profficialsite/tearsofsteel_1080p_60s_24fps.6000kbps.1920x1080.h264-8b.2ch.128kbps.aac.mp4) | `5f54703c66a86a8d6de7e8021d53de1cf5c82216d5c6361d832bdb85084d5984` |
| hevc_4k24 | hevc | [official test media](https://test.playready.microsoft.com/media/profficialsite/tearsofsteel_4k_60s_24fps.12000kbps.3840x2160.h265-8b.2ch.128kbps.aac.mp4) | `af69bb5d854a0f2106d59b57b6fe543faa1831a7ed83e3b9d6ecd8de52be962f` |

## 环境指纹

- Baseline label: `410ce6b`
- Baseline runner SHA-256: `29ac0f8e4aeb52b068ecc24aaf656ba4ed6b5f9b5741bdcf275b7ca35e1aa110`
- Current label: `e01d7cf`
- Current runner SHA-256: `1af155e2f80d4bdc365d29f663b1adb09928ef0f3fa82bec0fdd4603331b1216`
- Platform: `macOS-15.6.1-arm64-arm-64bit`
- Build type: `Release`
- Decode mode: software
- Held video frames: `3`
- Measured video frames per run: `240`

## 可重复执行

- Manifest SHA-256: `4abe3232d339c551eade9bc6fec582ba24ce8af145eb20fa7fc9c74d0fb1771b`
- Baseline results: `benchmark_artifacts/baseline`
- Current results: `benchmark_artifacts/current`
- Machine report: `docs/performance/video-picture-pool-benchmark-results.json`

所有原始单次 JSON 均保留 wall/user/system CPU、max RSS、帧 checksum、像素格式和
pool release 前后状态。媒体二进制不进入 Git，由 manifest URL 与 SHA-256 固定。
