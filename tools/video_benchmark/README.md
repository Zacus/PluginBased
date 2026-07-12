# Media SDK Video Benchmark

该工具对同一 SDK revision、真实编码媒体和进程级资源使用执行可重复测量。每个 runner
进程只执行一个 case 的一轮，避免 `ru_maxrss` 和 allocator 状态跨轮污染。

## 构建当前版本

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release \
  --target MediaSdkVideoBenchmark MediaSdkRealtimeVideoBenchmark \
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
  --media-dir benchmark_media
```

媒体二进制不提交到 Git。`media_manifest.json` 固定来源、SHA-256、测量帧数和超时。

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

```bash
python3 tools/video_benchmark.py compare \
  --baseline-dir benchmark_artifacts/baseline \
  --current-dir benchmark_artifacts/current \
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
  "tool": "Instruments Allocations + Time Profiler",
  "decoder_allocator_cpu_percent": 6.2,
  "decoder_allocator_allocation_percent": 18.4,
  "evidence": "artifact/path-or-run-id"
}
```

wall time 或 RSS 差异本身不能证明 FFmpeg decoder allocator 是瓶颈，因此没有归因报告时
门禁必然为 `CLOSED`。
