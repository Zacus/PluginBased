# Media SDK Video Benchmark

该工具对同一 SDK revision、真实编码媒体和进程级资源使用执行可重复测量。每个 runner
进程只执行一个 case 的一轮，避免 `ru_maxrss` 和 allocator 状态跨轮污染。

## 构建当前版本

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release \
  --target MediaSdkVideoBenchmark MediaSdkRealtimeVideoBenchmark MediaSdkGetBuffer2Benchmark \
           MediaSdkBenchmarkMediaGenerator --parallel
```

## 构建指定 SDK revision

benchmark 目录也可作为独立 CMake 工程，`PLUGINBASED_SOURCE_DIR` 指向待测 worktree：

```bash
cmake -S tools/video_benchmark -B build-benchmark-baseline \
  -DCMAKE_BUILD_TYPE=Release \
  -DPLUGINBASED_SOURCE_DIR=/path/to/baseline/worktree \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build-benchmark-baseline \
  --target MediaSdkVideoBenchmark MediaSdkRealtimeVideoBenchmark --parallel
```

runner 只使用对应 revision 可用的 SDK 接口；较旧 revision 没有 pool diagnostics 时，pool
字段保持为 0，但 wall/CPU/RSS、帧数和 checksum 仍可直接对比。

## 下载固定媒体

```bash
python3 tools/video_benchmark.py fetch \
  --media-generator build-release/tools/video_benchmark/MediaSdkBenchmarkMediaGenerator \
  --media-dir benchmark_media
```

媒体二进制不提交到 Git。`media_manifest.json` 固定来源、SHA-256、测量帧数和超时。
4K120 4:2:2 10-bit 压力 case 由固定的 `prores_ks` Standard 参数生成；生成结果同样校验
SHA-256，编码环境不一致时不会静默使用不同媒体。

## 执行

```bash
python3 tools/video_benchmark.py run \
  --runner build-benchmark-baseline/MediaSdkVideoBenchmark \
  --realtime-runner build-benchmark-baseline/MediaSdkRealtimeVideoBenchmark \
  --media-dir benchmark_media \
  --output-dir benchmark_artifacts/baseline \
  --label 410ce6b --runs 5 --warmups 1

python3 tools/video_benchmark.py run \
  --runner build-release/tools/video_benchmark/MediaSdkVideoBenchmark \
  --realtime-runner build-release/tools/video_benchmark/MediaSdkRealtimeVideoBenchmark \
  --media-dir benchmark_media \
  --output-dir benchmark_artifacts/current \
  --label current --runs 5 --warmups 1
```

## 门禁

先对当前 4K60 runner 执行 3 轮 Time Profiler。工具会验证每轮确实呈现 600 帧，导出
采样 XML，并把 FFmpeg allocator 直接符号占比和保守 CPU 上界写入 profile JSON：

```bash
python3 tools/video_allocator_profile.py record-time \
  --runner build-release/tools/video_benchmark/MediaSdkRealtimeVideoBenchmark \
  --input benchmark_media/bbb-4k60-hevc-main10.mp4 \
  --output-dir benchmark_artifacts/profiles/current-time-3run \
  --output docs/performance/video-picture-pool-allocator-profile.json \
  --label current-time-profile --runs 3
```

保守上界将所有 FFmpeg allocator/buffer 直接符号、所有 `libsystem_malloc.dylib` 样本以及
所有 `memset`/`bzero` 样本都归入 decoder allocator，并采用多轮最大值。该分类口径
有意高估，并通过三轮最大值降低采样偶然低估的风险；Allocations 数据不可用时必须写为
`null`，不能伪装成 0%。

```bash
python3 tools/video_benchmark.py compare \
  --baseline-dir benchmark_artifacts/baseline \
  --current-dir benchmark_artifacts/current \
  --allocation-profile docs/performance/video-picture-pool-allocator-profile.json \
  --output-json benchmark_artifacts/f1-gate.json \
  --output-markdown docs/performance/video-picture-pool-benchmark-results.md
```

F1 默认保持关闭。只有以下条件同时满足才输出 `OPEN`：

- 所有必选 case 的 baseline/current 均至少完成 5 轮；
- 每组帧数和 checksum 稳定；
- 当前版本释放 held frames 后 `inFlight=0`；
- 提供 allocator 归因 JSON，且 decoder allocator CPU 占比至少 5%，或 allocation 占比
  至少 15%。

allocator profile JSON 示例：

```json
{
  "tool": "Instruments Time Profiler",
  "decoder_allocator_cpu_percent": 4.04,
  "decoder_allocator_cpu_percent_semantics": "conservative_upper_bound_max",
  "decoder_allocator_allocation_percent": null,
  "decoder_allocator_allocation_percent_semantics": "unavailable"
}
```

wall time 或 RSS 差异本身不能证明 FFmpeg decoder allocator 是瓶颈，因此没有归因报告时
门禁必然为 `CLOSED`。

## get_buffer2 F1 实验

`MediaSdkGetBuffer2Benchmark` 直接使用 FFmpeg demux/decode，对比 FFmpeg default
allocator 与隔离的 prototype allocator。它不经过 `media_sdk::Player`，也不改变 SDK 或
PlayPlugin 默认行为。

```bash
build-release/tools/video_benchmark/MediaSdkGetBuffer2Benchmark \
  --input benchmark_media/bbb-4k60-hevc-main10.mp4 \
  --output benchmark_artifacts/get_buffer2/prototype.json \
  --label current --allocator prototype \
  --max-video-frames 600 --hold-video-frames 3
```

将 `--allocator prototype` 替换为 `default` 即得到同 runner、同 callback 层级的对照组。
两种模式必须先满足帧数和 checksum 一致，性能结果才有效。F1 设计和退出条件见
`docs/design/video-decoder-direct-rendering-f1.md`。

完整 F1 对照使用交替执行顺序，默认覆盖 H.264 1080p24、HEVC Main10 4K60，以及
synthetic ProRes 422 10-bit 4K120 压力 case：

```bash
python3 tools/get_buffer2_f1_benchmark.py \
  --runner build-release/tools/video_benchmark/MediaSdkGetBuffer2Benchmark \
  --media-dir benchmark_media \
  --output-dir benchmark_artifacts/get_buffer2-f1 \
  --output-json docs/performance/video-decoder-direct-rendering-f1-results.json \
  --output-markdown docs/performance/video-decoder-direct-rendering-f1-results.md \
  --label current --runs 5 --warmups 1
```

## get_buffer2 F2 生产路径验证

正式 SDK benchmark 可显式启用或关闭 decoder buffer pool，并输出
`decoder_pool_before_stop` / `decoder_pool_after_stop`：

```bash
./build/tools/video_benchmark/MediaSdkVideoBenchmark \
  --input benchmark_media/tearsofsteel-1080p24-h264.mp4 \
  --output benchmark_artifacts/get_buffer2-f2/h264-enabled.json \
  --label h264-enabled --software --max-video-frames 24 \
  --decoder-buffer-pool enabled
```

启用的软件解码 case 应满足 `pooled_frame_count >= frames.video`、`fallback_count == 0`；
关闭 case 的 decoder pool 统计应全部为 0。硬件解码不安装该 callback。
