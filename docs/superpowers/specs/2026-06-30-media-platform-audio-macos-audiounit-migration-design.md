# media_platform_audio_macos AudioUnit 迁移设计文档

## 背景

当前 `media_sdk_platform_audio_macos` 已经从 PlayPlugin 中独立出来，由 SDK runtime 通过 `IAudioOutput` 管理音频输出和音频主时钟。现有 `CoreAudioAudioOutput` 内部基于 `AudioQueue`：

- `CoreAudioAudioOutput` 负责 `open/write/clock/pause/resume/flush/close`。
- `CoreAudioRingBuffer` 是 SPSC PCM ring buffer，runtime 线程写入，CoreAudio 回调读取。
- `CoreAudioRingBuffer::read()` 和 `clock()` 已经约束为 lock-free/seqlock 快照路径。
- PlayPlugin 只在 composition root 创建 `CoreAudioAudioOutput` 并注入 `RuntimePlayer`。

问题是 `AudioQueue` 属于更高层、偏旧的 CoreAudio 输出 API，对低延迟、真实硬件回调控制、设备 clock/latency 查询、后续扩展 macOS 专用音频路径不如 AudioUnit 明确。企业级迁移不应临时堆一层 wrapper，而应把“音频输出引擎”和“macOS 设备 I/O”拆清楚，让 AudioUnit 成为正式设备 backend。

## 目标

1. 将 `CoreAudioAudioOutput` 内部输出设备从 `AudioQueue` 迁移到 macOS AudioUnit output unit。
2. 保持公开 SDK 合约不变：`media_sdk::platform::macos::CoreAudioAudioOutput` 仍实现 `runtime::IAudioOutput`。
3. 保持 PlayPlugin composition root 不变，避免上层因为平台实现迁移产生额外改动。
4. 保留并复用 `CoreAudioRingBuffer`，不重新发明 PCM 队列。
5. 将 realtime audio callback 路径约束为：不加锁、不分配内存、不写日志、不调用 Qt、不阻塞。
6. 建立内部可测试 seam，用 fake render device 验证生命周期、flush/generation、pause/resume 和 callback 消费，不依赖真实声卡。
7. 将 AudioQueue 禁止回归写入架构检查。
8. 保留音频主时钟语义：clock 表示硬件消费进度估计，不是 SDK 已写入字节数。

## 非目标

1. 本阶段不修改 `IAudioOutput` 公共接口。
2. 本阶段不迁移 Windows WASAPI、Linux ALSA/PulseAudio/PipeWire。
3. 本阶段不新增 Qt 音频输出 fallback。
4. 本阶段不把 AudioUnit 类型暴露到 SDK 公共 include。
5. 本阶段不改音频解码输出格式；仍要求 runtime 写入 interleaved Float32 PCM。
6. 本阶段不解决所有 macOS 设备切换策略，只建立默认输出设备的正确生命周期和错误边界。

## 目标架构

```text
media_sdk_playback_runtime
  IAudioOutput
    ^
    |
media_sdk_platform_audio_macos
  include/media_sdk/platform/macos/CoreAudioAudioOutput.h
    public SDK adapter, no AudioUnit headers

  src/CoreAudioAudioOutput.cpp
    owns CoreAudioOutputEngine through pimpl

  src/CoreAudioOutputEngine.h/.cpp
    validates IAudioOutput contract
    owns CoreAudioRingBuffer
    owns injected IAudioRenderDevice
    manages state/generation/pause/resume/flush/close

  src/AudioRenderDevice.h
    internal C++ interface for device start/stop/reset/close
    callback context is plain C function pointer + void*

  src/MacAudioUnitRenderDevice.h/.cpp
    owns AudioUnit
    creates kAudioUnitSubType_DefaultOutput
    installs render callback
    sets ASBD
    starts/stops/resets/disposes AudioUnit

  src/CoreAudioRingBuffer.h
    SPSC PCM queue and clock snapshot
```

依赖方向：

```text
CoreAudioAudioOutput -> CoreAudioOutputEngine -> AudioRenderDevice
MacAudioUnitRenderDevice -> CoreAudio / AudioUnit frameworks
CoreAudioRingBuffer -> runtime audio contracts only
runtime core -> no macOS concrete headers
PlayPlugin -> CoreAudioAudioOutput public header only
```

## API 与 ABI 边界

`CoreAudioAudioOutput.h` 保持现有公开类型和方法签名：

```cpp
class CoreAudioAudioOutput final : public runtime::IAudioOutput
{
public:
    CoreAudioAudioOutput();
    ~CoreAudioAudioOutput() override;

    CoreAudioAudioOutput(const CoreAudioAudioOutput&) = delete;
    CoreAudioAudioOutput& operator=(const CoreAudioAudioOutput&) = delete;

    [[nodiscard("inspect the CoreAudio open result before writing audio")]]
    Result<void> open(const runtime::AudioFormat& format) override;
    [[nodiscard("CoreAudio writes can reject stale generations or closed outputs")]]
    Result<void> write(runtime::AudioBufferView buffer) override;
    [[nodiscard("CoreAudio clock snapshots drive runtime A/V sync")]]
    runtime::ClockSnapshot clock() const override;
    void pause() override;
    void resume() override;
    void flush() override;
    void close() override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
```

内部新增的 `AudioRenderDevice` 不进入 public include，不承诺 ABI。它只服务于 macOS platform target 的单元测试和真实 AudioUnit backend。

## AudioUnit 设备策略

第一版使用 macOS default output AudioUnit：

- `AudioComponentDescription::componentType = kAudioUnitType_Output`
- `componentSubType = kAudioUnitSubType_DefaultOutput`
- `componentManufacturer = kAudioUnitManufacturer_Apple`
- 创建：`AudioComponentFindNext`、`AudioComponentInstanceNew`
- 设置输出格式：`AudioUnitSetProperty(kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &asbd, sizeof(asbd))`
- 设置 render callback：`AudioUnitSetProperty(kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof(callback))`
- 初始化：`AudioUnitInitialize`
- 启动：`AudioOutputUnitStart`
- 暂停/停止：`AudioOutputUnitStop`
- flush/reset：`AudioUnitReset(unit, kAudioUnitScope_Global, 0)`，同时清空 SDK ring buffer 并推进 generation
- 关闭：先停止，再 uninitialize，再 dispose

`AudioStreamBasicDescription` 第一版固定为 interleaved Float32：

```text
mFormatID = kAudioFormatLinearPCM
mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked
mFramesPerPacket = 1
mChannelsPerFrame = format.channels
mBitsPerChannel = 32
mBytesPerFrame = channels * sizeof(float)
mBytesPerPacket = mBytesPerFrame
```

## Realtime Callback 约束

AudioUnit render callback 是硬实时路径，必须满足：

- 不持有 `CoreAudioOutputEngine` mutex。
- 不调用 `std::condition_variable::wait`、`notify_all` 等可能阻塞的路径。
- 不分配内存，不扩容容器，不创建 `std::string`。
- 不写 spdlog/qDebug/stdout/stderr。
- 不调用 Qt API。
- 不调用 `AudioUnitStop`、`AudioUnitReset`、`AudioComponentInstanceDispose`。
- 只做三件事：检查 callback context、把 `ioData` 转为 byte span、调用 `CoreAudioRingBuffer::read()` 填充 PCM 或静音。

如果 `ioData` 包含多个 buffer，第一版只接受 interleaved 单 buffer。遇到不符合预期的 buffer layout，callback 填静音并增加 atomic 诊断计数，不能崩溃。

## 生命周期状态机

```text
Closed
  open(format) success
OpenStopped
  resume()
Running
  pause()
Paused
  resume()
Running
  flush()
OpenStopped or Running or Paused with generation+1, ring cleared, device reset
  close()
Closed
```

约束：

- `open()` 必须先 `close()` 旧设备，再创建新 AudioUnit。
- `open()` 失败后对象必须回到 `Closed`，`clock().valid == false`。
- `resume()` 只有在 `OpenStopped` 或 `Paused` 时启动 AudioUnit。
- `pause()` 不清空 ring buffer，只停止设备消费并标记 `clock().paused == true`。
- `flush()` 必须推进 generation，清空 ring buffer，reset AudioUnit；旧 generation 的 blocked writer 必须返回失败。
- `close()` 必须可重复调用，析构可安全调用，callback 不能在 device dispose 后访问已释放对象。

## 锁与线程模型

线程角色：

- Runtime audio producer thread：调用 `write()`，允许在 ring buffer 背压上等待，但必须能被 `flush()`/`close()` 唤醒。
- Qt GUI/thread：不应直接参与音频输出热路径。
- AudioUnit render thread：只调用 realtime-safe `read()`。
- Control caller thread：调用 `open/pause/resume/flush/close/clock`。

锁规则：

- `CoreAudioOutputEngine` 可以用普通 mutex 保护非实时状态和 device lifecycle。
- AudioUnit callback 不进入 engine mutex。
- `CoreAudioRingBuffer::read()` 和 `clock()` 保持 lock-free。
- `CoreAudioRingBuffer::write()` 可以使用 writer mutex，因为它只在 producer 线程。
- 所有 OSStatus 转换和日志只能发生在非 callback 路径。

## Clock 与 latency

`CoreAudioRingBuffer::clock()` 继续提供：

- `position`：已经被硬件 callback 消费的 PCM 时长对应的媒体时间。
- `queuedDuration`：SDK ring buffer 中还未被 callback 消费的音频时长。
- `generation`：当前 audio generation。
- `valid`：已 open 且未 close。
- `paused`：由 `CoreAudioOutputEngine` 覆盖。

AudioUnit 迁移后增加硬件 latency 查询：

- 优先读取 `kAudioUnitProperty_Latency`。
- 查询失败不使 `open()` 失败，`hardwareLatency` 保持 0，并记录一次非实时诊断。
- 不在 render callback 中查询 latency。

## 错误处理

所有 AudioUnit/CoreAudio 调用必须检查 `OSStatus`：

- `AudioComponentFindNext` 找不到默认输出：`UnsupportedFormat` 或 `InternalStateError`，message 指明 default output AudioUnit 不可用。
- `AudioComponentInstanceNew` 失败：`InternalStateError`，detail 包含 OSStatus。
- `AudioUnitSetProperty` 失败：`UnsupportedFormat` 或 `InternalStateError`，message 指明失败属性。
- `AudioUnitInitialize` 失败：释放已创建资源并回到 `Closed`。
- `AudioOutputUnitStart` 失败：保持 open 但不 running，`resume()` 不抛异常，诊断记录 start failure；后续 `close()` 必须可用。

错误 message 必须具体到失败阶段，不能只写 "CoreAudio failed"。

## 测试策略

必须新增或调整以下测试：

1. 架构检查：
   - platform audio target 不再包含 `AudioQueue`、`AudioQueueRef`、`AudioQueueNewOutput`、`AudioQueueEnqueueBuffer`、`AudioQueueStart`。
   - `MacAudioUnitRenderDevice` 必须包含 `AudioComponentFindNext`、`AudioComponentInstanceNew`、`AudioUnitSetProperty`、`AudioUnitInitialize`、`AudioOutputUnitStart`。
   - public header 不 include AudioUnit/CoreAudio headers。
   - runtime target 不 include concrete macOS audio output header。

2. Ring buffer 回归测试：
   - 保留 byte order、wrap、underrun silence、generation flush、close wakes writer、frame alignment 测试。
   - 增加 consumer 释放容量后的 writer 唤醒测试，避免重新引入固定 1ms 轮询作为长期背压策略。

3. Engine fake device 测试：
   - open 配置 fake device，并设置 render callback。
   - resume 调用 fake start。
   - fake callback 从 ring buffer 拉取写入 PCM。
   - pause 调用 fake stop 且不 flush queued PCM。
   - flush 推进 generation、reset fake device、旧 generation 写入失败。
   - close 停止并关闭 fake device，后续 callback 不访问已关闭 engine。

4. 真实 AudioUnit contract 测试：
   - invalid format 不创建设备。
   - 如果本机默认输出设备可用，Float32 stereo open/write/clock/pause/resume/flush/close 成功。
   - 如果 CI/headless 没有输出设备，测试必须报告可解释 skip，而不是假 pass。

## 性能与诊断

需要保留或新增以下诊断计数，至少在测试可读：

- render callback 次数。
- callback copied bytes。
- callback silence bytes。
- underrun 次数。
- start/stop/reset failure 次数。
- 当前 queued duration。
- 当前 generation。

性能验收：

- callback 内无锁、无分配、无日志。
- 4K60 视频播放时音频 callback 不应造成 runtime 大量 1ms 轮询 wake。
- seek/flush 后不会卡住 writer。
- pause/resume 多次调用不会卡死，不会访问 disposed AudioUnit。

## 回滚策略

迁移过程中不保留双实现运行时开关。原因是 AudioQueue 和 AudioUnit 两套真实设备输出并存会扩大状态矩阵，容易让 bug 被 fallback 掩盖。

可接受的回滚方式是 git revert 本迁移阶段提交，回到 AudioQueue 实现。为支持这个回滚，迁移必须保持：

- public `CoreAudioAudioOutput` header 不破坏上层。
- CMake target 名不变。
- PlayPlugin 注入代码不变。

## 验收标准

完成后必须满足：

1. `rg "AudioQueue|AudioQueueRef|AudioQueueNewOutput|AudioQueueEnqueueBuffer|AudioQueueStart" sdk/media_platform_audio_macos -n` 无源码命中。
2. `ctest --test-dir build --output-on-failure -R 'media_sdk_platform_audio_macos|media_sdk_playback_runtime_architecture_checks'` 通过。
3. `cmake --build build --parallel` 通过。
4. `ctest --test-dir build --output-on-failure` 通过。
5. 手工播放 `/Users/zs/Downloads/6月29日.mov`，随机 seek、pause/resume、播放结束均不出现无声、卡死或进度停住。
6. PlayPerf 日志中 renderer 仍持续出帧，audio clock 有效。

