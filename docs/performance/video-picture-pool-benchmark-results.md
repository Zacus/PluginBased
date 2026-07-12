# 视频帧对象池真实媒体 Benchmark 结果

## F1 门禁结论

**CLOSED：不启动 Decoder Direct Rendering F1。**

门禁原因：

- 未提供 decoder allocator 的 Allocations/Time Profiler 归因报告

门禁要求每个必选媒体至少完成清单指定的轮数、帧数和 checksum 稳定、释放后
`inFlight=0`，并提供 allocator 归因数据。仅有 wall time/RSS 对比不能证明
FFmpeg decoder allocator 是瓶颈。

## 对比结果

| Case | Runs | Wall baseline/current | Delta | CPU baseline/current | Delta | RSS baseline/current | Delta | Drop current | Lateness current | Pool acquire |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| h264_1080p24 | 5 | 251.78/255.57 ms | +1.50% | 1476.79/1477.88 ms | +0.07% | 103.50/103.55 MiB | +0.05% | 0 | 0 us | 0 |
| hevc_4k24 | 5 | 1007.79/1005.35 ms | -0.24% | 6878.34/6915.70 ms | +0.54% | 430.89/431.05 MiB | +0.04% | 0 | 0 us | 0 |
| hevc_4k60_realtime | 5 | 10670.07/10670.11 ms | +0.00% | 7418.00/7417.92 ms | -0.00% | 764.28/764.31 MiB | +0.00% | 0 | 1263 us | 0 |

core throughput case 观察 software decoder direct-output 路径；实时 case 通过
`PlaybackSession + RuntimePlayer + audio clock + presenter` 验证 60 fps 调度、队列、
late drop 和呈现延迟。wall/CPU/RSS 的中位数变化本身仍不足以证明 decoder allocator
是瓶颈。

## 测量范围

core runner 直接驱动 `media_sdk::Player` 并尽快解码；realtime runner 驱动
`PlaybackSession`，使用 mock audio device 的 PTS 时钟和同步 CPU presenter 实时调度。
两者均不包含 Qt Scene Graph 和真实 GPU texture upload。

| Case | Codec | Source | SHA-256 |
|---|---|---|---|
| h264_1080p24 | h264 | [official test media](https://test.playready.microsoft.com/media/profficialsite/tearsofsteel_1080p_60s_24fps.6000kbps.1920x1080.h264-8b.2ch.128kbps.aac.mp4) | `5f54703c66a86a8d6de7e8021d53de1cf5c82216d5c6361d832bdb85084d5984` |
| hevc_4k24 | hevc | [official test media](https://test.playready.microsoft.com/media/profficialsite/tearsofsteel_4k_60s_24fps.12000kbps.3840x2160.h265-8b.2ch.128kbps.aac.mp4) | `af69bb5d854a0f2106d59b57b6fe543faa1831a7ed83e3b9d6ecd8de52be962f` |
| hevc_4k60_realtime | hevc-main10 | [official test media](https://test.playready.microsoft.com/media/profficialsite/bbb-3840x2160-cfg02-frag-6mbps.mp4) | `456cdf865a5416a7661bdece55c47b67f85ebff0287595bf9af34211972e4b43` |

## 环境指纹

- Baseline label: `410ce6b`
- Baseline runner SHA-256: `29ac0f8e4aeb52b068ecc24aaf656ba4ed6b5f9b5741bdcf275b7ca35e1aa110`
- Current label: `e01d7cf`
- Current runner SHA-256: `1af155e2f80d4bdc365d29f663b1adb09928ef0f3fa82bec0fdd4603331b1216`
- Platform: `macOS-15.6.1-arm64-arm-64bit`
- Build type: `Release`
- Decode mode: software
- Core held video frames: `3`
- Core measured video frames per run: `240`

## 可重复执行

- Manifest SHA-256: `b40e6ca0bc242d443d863f2d798ed510db8cb40aa2918c1df4e7b424a21fe732`
- Baseline results: `benchmark_artifacts/baseline`
- Current results: `benchmark_artifacts/current`
- Machine report: `docs/performance/video-picture-pool-benchmark-results.json`

所有原始单次 JSON 均保留 wall/user/system CPU、max RSS、帧 checksum 和对应 runner
可观测的队列、延迟及对象池状态。媒体二进制不进入 Git，由 manifest URL 与
SHA-256 固定。
