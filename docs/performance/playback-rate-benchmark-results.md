# 倍速播放真实媒体 Benchmark 结果

## 数据门禁

**PASS**

| Case | Runs | Wall median | Worst error | Pitch error | A/V drift max | Drops | Underflow | Late writes | CPU median | RSS median |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| continuous-change_audio_1_5 | 3 | 3761.4 ms | 0.00% | 0.10% | 0.00 ms | 0 | 0 | 0 | 49.3 ms | 15.6 MiB |
| legacy_audio_1_0 | 3 | 4001.7 ms | 0.15% | 0.04% | 0.00 ms | 0 | 0 | 0 | 29.7 ms | 12.2 MiB |
| legacy_av_1_0 | 3 | 8192.5 ms | 2.42% | 0.00% | 3.07 ms | 0 | 0 | 188 | 2162.8 ms | 121.6 MiB |
| legacy_video_1_0 | 3 | 3926.9 ms | 1.86% | 0.00% | 0.00 ms | 0 | 0 | 0 | 103.1 ms | 21.4 MiB |
| paused-change_audio_1_5 | 3 | 3117.5 ms | 0.00% | 0.08% | 0.00 ms | 0 | 0 | 0 | 38.1 ms | 14.2 MiB |
| playing-change_audio_1_5 | 3 | 3103.2 ms | 0.00% | 0.01% | 0.00 ms | 0 | 0 | 0 | 39.6 ms | 15.0 MiB |
| seek-change_audio_1_5 | 3 | 2452.2 ms | 0.00% | 0.05% | 0.00 ms | 0 | 0 | 0 | 28.9 ms | 14.2 MiB |
| steady_audio_0_5 | 3 | 8006.2 ms | 0.08% | 0.01% | 0.00 ms | 0 | 0 | 0 | 95.0 ms | 15.7 MiB |
| steady_audio_1_0 | 3 | 4004.0 ms | 0.18% | 0.04% | 0.00 ms | 0 | 0 | 0 | 29.8 ms | 12.8 MiB |
| steady_audio_1_5 | 3 | 2672.1 ms | 0.28% | 0.03% | 0.00 ms | 0 | 0 | 0 | 38.6 ms | 13.8 MiB |
| steady_audio_2_0 | 3 | 2005.0 ms | 0.27% | 0.02% | 0.00 ms | 0 | 0 | 0 | 31.6 ms | 13.3 MiB |
| steady_av_0_5 | 3 | 16475.9 ms | 2.99% | 0.00% | 1.55 ms | 0 | 0 | 206 | 2906.5 ms | 127.0 MiB |
| steady_av_1_0 | 3 | 8193.4 ms | 2.47% | 0.00% | 34.07 ms | 0 | 0 | 188 | 2183.9 ms | 122.3 MiB |
| steady_av_1_5 | 3 | 5507.2 ms | 3.39% | 0.00% | 4.53 ms | 0 | 0 | 202 | 1755.0 ms | 120.6 MiB |
| steady_av_2_0 | 3 | 4131.9 ms | 3.46% | 0.00% | 6.03 ms | 0 | 0 | 201 | 1526.8 ms | 120.6 MiB |
| steady_video_0_5 | 3 | 7842.1 ms | 1.98% | 0.00% | 0.00 ms | 0 | 0 | 0 | 110.2 ms | 21.4 MiB |
| steady_video_1_0 | 3 | 3926.9 ms | 1.83% | 0.00% | 0.00 ms | 0 | 0 | 0 | 104.3 ms | 21.4 MiB |
| steady_video_1_5 | 3 | 2619.0 ms | 1.80% | 0.00% | 0.00 ms | 0 | 0 | 0 | 98.6 ms | 21.4 MiB |
| steady_video_2_0 | 3 | 1965.1 ms | 1.81% | 0.00% | 0.00 ms | 0 | 0 | 0 | 96.8 ms | 21.4 MiB |

## 1.0x 兼容路径对比

| Media | CPU | RSS | Startup |
|---|---:|---:|---:|
| av | +0.98% | +0.58% | -1.30% |
| audio | +0.31% | +4.74% | +0.00% |
| video | +1.15% | -0.15% | -0.52% |

兼容基线使用同一 runner 的默认 1.0x 路径，显式 1.0x 路径不得超过 10% 回退。
每轮在独立进程中执行；同时记录稳态中点到结束的 RSS，增长超过 16 MiB 即失败。

## 环境与复现

- Label: `feature-r6-release`
- Platform: `macOS-15.6.1-arm64-arm-64bit`
- Machine: `MacBook Air Mac15,12`
- Processor: `Apple M3`
- Memory: `16.0 GiB`
- Runner SHA-256: `a11531e80bc7118c7750a56ecb0c5a82772432d0b1e19cbf88a39c42acb4a0fe`
- Build type: `Release`
- Runs per case: `3`
- Media window: `8000 ms`
- Raw results: `/Users/zs/Downloads/PluginBased/benchmark_artifacts/playback-rate-r6`
- audio: PCM s16le 48000 Hz stereo 440 Hz; `/Users/zs/Downloads/PluginBased/benchmark_media/playback-rate-440hz.wav` (`3cfb12394a1e7e570a2b4d16a16be61e059ef40be76f7ebdc511978d259d7b5e`)
- av: H.264 1920x1080 24 fps + AAC stereo; `/Users/zs/Downloads/PluginBased/benchmark_media/tearsofsteel-1080p24-h264.mp4` (`5f54703c66a86a8d6de7e8021d53de1cf5c82216d5c6361d832bdb85084d5984`)
- video: MPEG-4 640x360 30 fps; `/Users/zs/Downloads/PluginBased/benchmark_media/playback-rate-video-only.mp4` (`975b0b361d2d663f43749bc63d2d45fca115fd65e62b9a011481e791afc4b818`)

复现命令：

```bash
python3 tools/playback_rate_benchmark.py run \
  --runner /Users/zs/Downloads/PluginBased/build-release/tools/video_benchmark/MediaSdkRealtimeVideoBenchmark \
  --output-dir /Users/zs/Downloads/PluginBased/benchmark_artifacts/playback-rate-r6 \
  --output-json docs/performance/playback-rate-benchmark-results.json \
  --output-markdown docs/performance/playback-rate-benchmark-results.md \
  --label feature-r6-release --build-type Release \
  --runs 3 --window-ms 8000
```

原始结果位于 benchmark 输出目录，每个 JSON 保留完整命令、wall/user/system CPU、
max/current RSS、音调、A/V drift、late drop、underflow 和 runtime tempo 诊断。
`underflow` 只统计输出层明确报告的 underrun；无设备实时 sink 的调度偏差单独记为
`late_write_count`，不冒充 CoreAudio 硬件 underrun。
发布前仍需在真实 CoreAudio 设备路径补充 underrun 遥测；该限制不改变本报告对 SDK
时间线、保调变速、A/V 同步和资源门禁的 PASS 结论。
