# 视频帧对象池验证记录

## 验证范围

验证对象为 CPU 视频转换帧池、SDK/runtime/session 诊断传递、PlayPlugin 共享帧引用以及
seek、fallback、EOF、clear、stop 和重复关闭时的生命周期。

验证环境：

- 日期：2026-07-11
- 平台：macOS arm64
- 构建目录：`build`
- 构建类型：沿用当前 build 目录配置

## 自动验证

执行：

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

结果：完整构建成功，47/47 CTest 通过。

对象池专项压力验证：

```bash
ctest --test-dir build \
  -R 'playplugin_video_frame_bridge|media_sdk_playback_runtime_player_mock' \
  --repeat until-fail:10 --output-on-failure
```

结果：10 次连续运行通过。并发 pool acquire/release 测试覆盖 8 个线程、每线程 400 次
借还，未观察到重复借出，结束后 in-flight 为 0，retained 未超过 capacity。

## 已覆盖行为

- 同格式转换帧在预热后复用已有 AVFrame 和像素 buffer；
- 同一槽位不会在仍有共享引用时再次借出；
- pool 满时创建 transient frame，不等待 runtime/presenter；
- 分辨率变化推进 format epoch，旧 epoch 在途帧归还后释放；
- `VideoFrameProcessor::reset()` 推进稳定 PoolState 的 epoch，旧帧归还仍会更新当前 gauge；
- seek tail、runtime queue、presenter 和模拟 Scene Graph 引用覆盖完整 storage 生命周期；
- PlayPlugin 不再调用 `av_frame_clone()`，CPU/native frame 共用 SDK storage 控制块；
- clear 丢弃 pending/current frame，stop 不等待在途 pool frame；
- seek 保留上一帧直到新 generation 帧替换，旧帧不会提前回收；
- EOF drain 完成后最后显示帧保持有效，stop/clear 后释放；
- native-to-CPU fallback 能为转换输出初始化兼容的 CPU pool；
- pool diagnostics 只观察，不参与播放策略，audio-only 不输出全零 pool 日志。
- pool 统计不再随 `DecodeFrameMetadata` 逐帧传递；周期 report 和按需 session 查询分别
  覆盖性能趋势与实时 `retained`/`in-flight` 状态。

## 分配结果

测试确认默认 `initialRetained=3` 时：

- 首个需要转换的帧创建 3 个 retained buffer；
- 后续同 key 转换增加 reuseCount，不增加 allocationCount；
- 支持的 YUV420P/NV12 和 VideoToolbox native 路径不访问 CPU picture pool；
- software decoder 直出仍保持原有零拷贝路径。

这些结果证明了分配行为变化，但不等同于端到端性能提升结论。

## 真实媒体 benchmark

已增加 `tools/video_benchmark.py`、独立 C++ runner 和固定媒体 manifest，并在同一台
macOS arm64 机器上对 `410ce6b` 与 `e01d7cf` 的 Release software decode 各执行 5 轮：

- H.264 1080p24：wall `+1.50%`、CPU `+0.07%`、RSS `+0.05%`；
- HEVC 4K24：wall `-0.24%`、CPU `+0.54%`、RSS `+0.04%`；
- HEVC Main10 4K60 实时流水线：wall `+0.00%`、CPU `-0.00%`、RSS `+0.00%`；
- 每轮测量 240 个视频帧，baseline/current 的帧数和 checksum 一致；
- 4K60 每轮按音频 PTS 时钟呈现 600 帧，5 轮 checksum 一致且 late drop 均为 0；
- 当前版本 4K60 平均呈现延迟中位数为 `1.263 ms`，视频队列高水位为 8；
- 三个 case 均走 software decoder direct-output，pool acquire 为 0，符合零拷贝旁路设计。

4K60 case 驱动 `PlaybackSession + RuntimePlayer`，使用 mock audio device 提供实时 PTS
时钟并由同步 CPU presenter 消费视频帧。它补充验证了调度、队列背压和 late drop，但
仍不包含 Qt Scene Graph、GPU texture upload 或真实音频设备。

完整环境指纹、媒体 SHA-256、统计 JSON 和复现命令见
`docs/performance/video-picture-pool-benchmark-results.md`。

仍未执行以下 UI/GPU 手工验证：

- 真实窗口播放的 GPU present latency、系统负载和 drop count 对比；
- VideoToolbox 到真实 Metal texture 的人工画面检查；
- 连续拖动 seek、切换 Space、窗口隐藏/恢复的视觉检查；
- native presenter failure 后真实媒体 CPU fallback 的画面连续性。

## 已知非本任务问题

完整构建仍报告 `CoreAudioOutputEngine.cpp` 忽略 `CoreAudioRingBuffer::read()` 的
`[[nodiscard]]` 返回值。该警告在本任务前已存在，与视频帧池无关，本次未修改。

## Decoder Direct Rendering 门禁

真实媒体吞吐与 4K60 实时对比均未观察到可归因于 decoder allocator 的 wall/CPU/RSS、
呈现延迟或丢帧改善。当前版本另执行 3 轮 Instruments Time Profiler：直接 FFmpeg
allocator/buffer 符号 CPU 占比中位数为 `0.054%`；把所有 `libsystem_malloc`、
`memset` 和 `bzero` 也计入后，三轮保守上界最大值为 `4.044%`，仍低于 5% 门槛。

Allocations 模板在当前环境无法附加目标进程，因此 allocation 占比明确记为不可用，
而不是 0%。CPU 保守上界不足以按数据门禁启动 F1。归因明细和 trace/XML 指纹见
`docs/performance/video-picture-pool-allocator-profile.json`。

2026-07-14 在明确授权下仍执行了隔离 F1 原型。原型的 H.264/HEVC 帧数与 checksum
一致，生命周期和 fallback 测试通过，但 H.264 wall/CPU 仅改善 `0.74%/0.08%`，HEVC
4K60 wall 改善 `0.34%`、CPU 回退 `0.07%`。结果低于 3% 门槛，F1 判定 NO-GO，
不启动 F2，不修改 SDK/PlayPlugin 默认路径。

只有未来平台或媒体的固定基准显示 decoder allocator 对 latency、CPU 或内存抖动有可重复
显著影响时，才重新评估该 NO-GO 结论。
