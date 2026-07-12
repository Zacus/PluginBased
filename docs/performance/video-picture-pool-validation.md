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

## 未执行的手工验证

当前执行环境没有固定的 1080p/4K 性能媒体、可交互窗口流程和可复现的 GPU/系统负载，
因此未声明以下项目已验证：

- 真实 1080p/4K 长时间播放的 RSS、CPU、decode latency 和 drop count 对比；
- VideoToolbox 到真实 Metal texture 的人工画面检查；
- 连续拖动 seek、切换 Space、窗口隐藏/恢复的视觉检查；
- native presenter failure 后真实媒体 CPU fallback 的画面连续性。

## 已知非本任务问题

完整构建仍报告 `CoreAudioOutputEngine.cpp` 忽略 `CoreAudioRingBuffer::read()` 的
`[[nodiscard]]` 返回值。该警告在本任务前已存在，与视频帧池无关，本次未修改。

## Decoder Direct Rendering 门禁

当前没有数据证明 software decoder 像素分配是剩余主要瓶颈。基础实现已经消除 SDK
格式转换路径预热后的逐帧像素分配，同时保留 decoder 直出零拷贝。因此不启动
`AVCodecContext::get_buffer2` 原型和正式实现，里程碑 F 保持关闭。

只有在固定媒体基准显示 decoder allocator 对 latency、CPU 或内存抖动有可重复显著影响
时，才重新开启 F1，并为 codec alignment、frame threading、硬件回调和 fallback 单独
编写设计补充。
