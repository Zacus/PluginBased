# Media SDK C 阶段总体设计文档

## 背景

B+ 阶段已经把播放器从 PlayPlugin 内部播放管线逐步迁向 SDK：

- `media_sdk_core` 负责打开媒体、demux、decode、硬解 backend、frame contract、控制事件和解码帧通道。
- `media_sdk_playback_runtime` 已经具备音频队列、视频队列、A/V 同步、presenter 背压、EOF drain、native fallback 和 runtime diagnostics。
- `media_sdk_platform_audio_macos` 已经提供基于 AudioUnit 的 `CoreAudioAudioOutput`，由 SDK runtime 管理音频输出和 audio clock。
- PlayPlugin 保留 QML、Qt facade、Qt RHI presenter、`FFmpegSurface`、Scene Graph 渲染和插件 UI。

当前架构已经具备进入 C 阶段的基础，但还没有完成 C 阶段目标。主要原因是 SDK 的 core player 和 playback runtime 仍由 PlayPlugin 的 `SdkPlaybackAdapter` 组合：

```text
PlayPlugin::SdkPlaybackAdapter
  owns media_sdk::Player
  owns/shared media_sdk::runtime::RuntimePlayer
  implements IEventSink
  implements IDecodeFrameSink
  implements IRuntimePlayerEvents
  maps core timeline -> runtime timeline
  maps seek/fallback/eof/control event -> Qt signal
```

也就是说，B+ 已经把“数据面”和“控制面”拆开，但 C 阶段的“播放会话编排权”还在 PlayPlugin。C 阶段要把这部分正式移入 SDK，让 PlayPlugin 只做展示和 Qt 输入适配。

## 当前构成评估

### `media_sdk_core`

现有职责：

- `Player` 是 SDK core 的公开 facade。
- `PlaybackController` 只转发命令到 `DecodeWorker`。
- `DecodeWorker` 使用单 worker 线程处理 open/play/pause/seek/stop、demux、audio decode、video decode 和 frame push。
- `IEventSink` 只发送控制事件。
- `IDecodeFrameSink` 发送 `AudioFrame` / `VideoFrame`，并返回结构化背压结果。
- `VideoFrameProcessor` 负责 native frame、CPU transfer、SWS normalize 和 SDK `VideoFrame` 创建。
- `HardwareDecoderFactory` 当前只在 macOS 下尝试 `VideoToolboxBackend`。

约束：

- `media_sdk_core` Qt-free。
- 当前不拆 demux/audio decode/video decode 多线程。
- `DecodeWorker::emitEvent()` 仍同步调用事件 sink，因此事件 sink 必须快速返回。
- EOF 目前仍由 core 通过控制事件发出，再由 PlayPlugin 转给 runtime drain。

主要问题：

- `media_sdk::Player` 不知道 runtime，SDK 没有统一播放会话 facade。
- fallback 后重开 core player 的策略在 PlayPlugin 中实现。
- EOF 的“decode finished”和“presented/drained finished”语义分散在 core、runtime 和 PlayPlugin。
- 音频格式兜底转换仍在 PlayPlugin `SdkPlaybackAdapter` 中完成。

### `media_sdk_playback_runtime`

现有职责：

- `RuntimePlayer` 管理音频/视频队列。
- `RuntimeFrameQueue` 支持容量、阻塞背压、abort、generation 校验、高水位。
- `AvSyncScheduler` 根据 audio clock 决定 wait/render/drop。
- `PresentTracker` 管理 presenter pending frame 和 completion。
- `NativeFallbackController` 管理 native presenter failure 后 fallback 到 CPU path。
- `RuntimeDiagnostics` 已经覆盖 native/cpu、队列、背压、drop/wait、EOF 等计数。

约束：

- Runtime Qt-free。
- Runtime 不拥有 Qt presenter，只依赖 `IVideoPresenter`。
- Runtime 不创建平台音频设备，只依赖 `IAudioOutput`。
- Runtime 目前不知道 `media_sdk::Player`，只接收外部 enqueue 的 runtime frame。

主要问题：

- `RuntimePlayer` 是“播放数据 runtime”，不是完整“播放 session runtime”。
- session/generation 与 core metadata 的映射仍在 PlayPlugin。
- fallback 请求通过 `IRuntimePlayerEvents` 回到 PlayPlugin，再由 PlayPlugin 重建 core player。
- `RuntimePlayer::open()` 当前直接启动 audio output，即使媒体可能无音频，需要更明确的 no-audio clock policy。

### `media_sdk_platform_audio_macos`

现有职责：

- `CoreAudioAudioOutput` 实现 `IAudioOutput`。
- `CoreAudioOutputEngine` 管理 open/write/clock/pause/resume/flush/close。
- `CoreAudioRingBuffer` 是 SPSC lock-free PCM ring buffer。
- AudioUnit callback 走实时音频路径，不依赖 Qt。

约束：

- 平台音频模块依赖 runtime interface，不依赖 PlayPlugin。
- CoreAudio output 生命周期由当前 composition root 注入。

主要问题：

- 平台 factory/composition 还在 PlayPlugin。
- no-audio 文件、audio output open failure、device lost 等策略没有统一收敛到 SDK session 层。

### PlayPlugin

现有职责：

- QML UI、播放列表、控制条、QML-facing `PlayerEngine`。
- `PlaybackPipeline` 组合 Qt presenter、audio output、SDK adapter。
- `SdkPlaybackAdapter` 组合 core player 和 runtime player。
- `QtRhiVideoPresenter` 实现 `IVideoPresenter`，通过 `QQuickWindow::afterRendering` 形成正式 present completion 闭环。
- `FFmpegSurface`、`VideoNode`、`VideoMaterial` 管理 Qt Scene Graph 和 RHI texture。

约束：

- Qt RHI、QQuickWindow、QSGNode、QRhi 不进入 SDK core/runtime 公共接口。
- PlayPlugin 可以实现 presenter，但不应决定 A/V 同步、队列背压、fallback 策略。

主要问题：

- `SdkPlaybackAdapter` 同时承担 SDK session 编排、timeline 映射、fallback、frame sink、event sink、Qt signal 转发，职责过重。
- PlayPlugin 中仍保留旧 `AudioRenderer`、`VideoRenderer`、`VideoFrameScheduler`、`ClockSync` 等历史播放组件，容易造成维护误判。
- `SdkPlaybackAdapter::runtimeAudioFrame()` 仍做 sample format 转换，SDK 音频输入契约不够自洽。

## C 阶段目标

C 阶段目标是让 SDK 拥有完整播放会话编排权：

```text
PlayPlugin
  QML / PlayerEngine / QtRhiVideoPresenter / FFmpegSurface
        |
        v
media_sdk_playback_session
  PlaybackSession facade
  core Player + runtime RuntimePlayer composition
  control event routing
  frame sink routing
  seek/fallback/eof/generation coordination
        |
        +--> media_sdk_core
        +--> media_sdk_playback_runtime
        +--> injected IAudioOutput
        +--> injected IVideoPresenter
```

最终目标：

1. SDK 管 `media_sdk::Player` 与 `runtime::RuntimePlayer` 的生命周期。
2. SDK 管 session/generation/timeline 映射。
3. SDK 管 seek、fallback、EOF drain、pause/resume/stop 的跨 core/runtime 一致性。
4. SDK 管音频输出 runtime 和 audio clock。
5. SDK 管 video queue、presenter completion 背压和 native/cpu fallback。
6. PlayPlugin 只负责 Qt presenter、QML 状态绑定、用户输入转发。

## 非目标

C 阶段第一轮不做以下事情：

1. 不把 Qt RHI、QQuickItem、QSGNode、QRhi、QObject 放进 SDK core/runtime 公共接口。
2. 不拆 demux/audio decode/video decode 多线程。
3. 不重写 VideoToolbox backend。
4. 不做 Windows D3D11、Linux VAAPI、OpenGL presenter。
5. 不承诺稳定二进制 ABI；先稳定源码级 C++20 API。
6. 不删除旧 PlayPlugin 类，先断开主链路依赖，再清理历史代码。
7. 不用无界异步队列、timer retry 或增大缓存掩盖背压问题。

## 新增模块建议

新增 `sdk/media_playback_session`，作为 C 阶段的 SDK 组合层。

原因：

- `media_sdk_core` 应继续专注 demux/decode，不应反向依赖 runtime。
- `media_sdk_playback_runtime` 应继续专注 A/V 队列、时钟、present，不应知道 demux/decode。
- session 组合层可以同时依赖 core 和 runtime，并向 PlayPlugin 暴露一个稳定 facade。

建议依赖方向：

```text
media_sdk_core
        ^
        |
media_sdk_playback_runtime
        ^
        |
media_sdk_playback_session
        ^
        |
PlayPlugin
```

`media_sdk_platform_audio_macos` 仍作为平台实现，由 PlayPlugin 或未来 app composition root 创建后注入 `PlaybackSession`。

## 公开接口草案

### `PlaybackSession`

```cpp
namespace media_sdk::session {

class ISessionEvents {
public:
    virtual ~ISessionEvents() = default;
    virtual void onEvent(const PlayerEvent& event) = 0;
    virtual void onRuntimeDiagnostics(runtime::RuntimeDiagnostics diagnostics) {}
};

struct PlaybackSessionConfig {
    PlayerConfig core;
    runtime::RuntimePlayerConfig runtime;
    bool preferNativeVideoFrames = true;
};

struct PlaybackSessionDependencies {
    runtime::IAudioOutput* audioOutput = nullptr;
    runtime::IVideoPresenter* videoPresenter = nullptr;
    ISessionEvents* events = nullptr;
};

class PlaybackSession final {
public:
    PlaybackSession(PlaybackSessionConfig config,
                    PlaybackSessionDependencies dependencies);
    ~PlaybackSession();

    PlaybackSession(const PlaybackSession&) = delete;
    PlaybackSession& operator=(const PlaybackSession&) = delete;

    [[nodiscard("open result determines whether playback session is usable")]]
    Result<void> open(const std::filesystem::path& path);
    void play();
    void pause();
    void stop();
    [[nodiscard("seek result determines whether the seek command was accepted")]]
    Result<void> seek(std::chrono::milliseconds position);
    [[nodiscard("timeline is required for diagnostics and stale callback checks")]]
    runtime::RuntimeTimeline timeline() const;
    [[nodiscard("diagnostics are required to verify native/cpu/fallback/backpressure behavior")]]
    runtime::RuntimeDiagnostics diagnostics() const;
};

} // namespace media_sdk::session
```

### 事件语义

`PlaybackSession` 对外仍使用 `PlayerEvent`，但事件语义改为 session 级：

- `MediaInfoEvent`：媒体已打开，runtime 已准备好接收帧。
- `StateChangedEvent::Playing`：core 已播放，runtime 已 resume。
- `StateChangedEvent::Paused`：core 已暂停，runtime 已 pause。
- `SeekCompletedEvent`：core seek 完成，runtime generation 已完成对齐，可以接收新帧。
- `EndOfFileEvent`：runtime audio/video EOF 均 drain/present 完成后再对外发出。
- `ErrorEvent`：core/runtime/audio/presenter 任一关键错误都会汇总为 session error。

这会改变当前 EOF 语义：不再把 core decode EOF 直接作为 UI 终态，而是以 runtime drain 完成为最终 EOF。

### 帧通道语义

`PlaybackSession` 内部实现 `IDecodeFrameSink`，把 core frame 转成 runtime frame。

约束：

- core frame push 只进入 session 内部，不暴露给 PlayPlugin。
- audio sample format 转换必须在 SDK 内完成，PlayPlugin 不再转换 PCM。
- session 必须用 core `EventMetadata` 和 runtime `RuntimeTimeline` 做 generation 映射。
- seek/fallback/stop/open 必须取消旧 generation 阻塞 push。

### fallback 语义

当前 fallback 链路：

```text
RuntimePlayer detects native presenter failure
  -> IRuntimePlayerEvents::onFallbackToCpuRequested
  -> PlayPlugin::SdkPlaybackAdapter resets core player
  -> seek to resume position
```

C 阶段目标链路：

```text
RuntimePlayer detects native presenter failure
  -> PlaybackSession handles fallback action
  -> session reconfigures core player preferNativeVideoFrames=false
  -> session seeks to runtime resume position
  -> PlayPlugin receives nativeRenderingFailed/session warning event only
```

PlayPlugin 不再参与 fallback 决策，只展示状态或错误。

### 音频策略

`PlaybackSession` 必须统一处理：

- 有音频：默认 audio clock 为主时钟。
- 无音频：runtime 使用 video/self clock，不要求打开真实 audio output。
- audio open/resume failure：返回 open failure 或切到 documented no-audio fallback，不能静默继续。
- pause/resume/seek/stop：session 同步调用 core 和 runtime，并保证 generation 递增一致。

### 硬解和零拷贝策略

当前 `VideoToolboxBackend` 已经能产出 `PixelFormat::Native` + `NativeHandleKind::VideoToolboxPixelBuffer`。

C 阶段约束：

- 硬解 backend 仍在 SDK 内部。
- presenter 只通过 `IVideoPresenter::capabilities()` 声明是否支持 native。
- session/core 根据 runtime output policy 设置 `PlayerConfig::preferNativeVideoFrames`。
- native presenter failure 后由 session 触发 CPU fallback。
- PlayPlugin 不再决定是否重开 core player，只提供 capabilities 和 completion。

## 线程模型

保持当前线程模型，不在 C 阶段第一轮拆更多 decode 线程：

```text
Qt GUI / QML thread
  PlayerEngine
  QtRhiVideoPresenter marshal target
  FFmpegSurface / Scene Graph

SDK session owner thread
  PlaybackSession public method caller
  lightweight state lock

media_core DecodeWorker thread
  command queue
  demux
  decode
  frame push into PlaybackSession
  control event into PlaybackSession

runtime audio thread
  pop RuntimeAudioFrame
  write IAudioOutput

runtime video thread
  pop RuntimeVideoFrame
  A/V sync wait/drop/render
  call IVideoPresenter

CoreAudio realtime callback thread
  consume lock-free PCM ring buffer
```

线程约束：

- PlayPlugin 不能在 GUI thread 上执行阻塞 frame push。
- `PlaybackSession` 内部不能在持锁状态下调用 Qt presenter、audio output、core player blocking API。
- `IEventSink` 对外事件必须可被 PlayPlugin queued 到 GUI thread。
- Qt presenter 只能在内部 marshal 到合法 Qt thread，SDK 不感知 Qt thread。
- realtime audio callback 不加 mutex、不做分配、不调用 Qt。

## 生命周期模型

对象所有权：

- PlayPlugin owns `PlaybackSession`。
- PlayPlugin owns concrete `CoreAudioAudioOutput` and `QtRhiVideoPresenter` in phase C1/C2。
- `PlaybackSession` owns `media_sdk::Player` and `runtime::RuntimePlayer`。
- `PlaybackSession` stores injected output/presenter/events as non-owning pointers.
- `RuntimePlayer` does not own audio output or presenter.
- `media_sdk::Player` owns `DecodeWorker` and stops it on destruction.

关闭顺序：

1. PlayPlugin calls `PlaybackSession::stop()` or destroys session.
2. Session stops core player command/decode worker.
3. Session stops runtime, aborting audio/video queues and presenter waits.
4. Runtime joins audio/video workers.
5. Runtime pauses/flushes/closes audio output and clears presenter.
6. Session releases core/runtime objects.
7. PlayPlugin may destroy presenter/surface/audio output.

## 错误模型

所有 SDK API 继续使用 `Result<T>`。

错误来源：

- core open/decode/seek failure。
- runtime dependency missing。
- audio output open/resume/write failure。
- presenter failure/device lost/unsupported native handle。
- fallback failure。
- stale generation or closed frame channel。

错误原则：

- 可恢复 native presenter failure 走 fallback。
- fallback 后仍失败，发 session `ErrorEvent`。
- audio device open failure 默认视为 open failure；后续可增加显式 no-audio policy。
- stale generation 不作为用户错误，只记录 diagnostics。
- closed/cancelled frame push 不应导致崩溃。

## 诊断要求

保留并扩展当前 diagnostics：

- core decoded frames、native frames、hardware transfers、normalize cost。
- runtime audio/video queued、backpressure count、wait us、高水位。
- video waited、dropped late、presented、native/cpu presented。
- presenter native texture created/drawn/failed。
- fallback count、fallback reason、fallback resume position。
- EOF accepted/presented。
- session open/seek/fallback generation 映射。

PlayPlugin 只读取并展示或记录 diagnostics，不维护策略计数。

## 迁移策略

C 阶段按“小步迁移、每阶段可提交、可回滚”执行：

1. 先新增 `media_sdk_playback_session`，不删除 `SdkPlaybackAdapter`。
2. 将 `SdkPlaybackAdapter` 当前逻辑复制/迁移为 SDK Qt-free session tests 能覆盖的逻辑。
3. PlayPlugin 切换到 `PlaybackSession` 后，`SdkPlaybackAdapter` 只保留 Qt signal adapter。
4. fallback、EOF、seek 语义稳定后，再清理 PlayPlugin 旧桥接和历史播放组件。

## 验收标准

C 阶段第一轮完成后必须满足：

- `SdkPlaybackAdapter` 不再实现 `IDecodeFrameSink`。
- `SdkPlaybackAdapter` 不再创建 `media_sdk::Player` 和 `runtime::RuntimePlayer`。
- PlayPlugin 不再做 audio sample format 转换。
- PlayPlugin 不再处理 native fallback 重开 core player。
- EOF 对 UI 的完成语义来自 runtime drain/present completion。
- CI `cmake --build build --parallel` 通过。
- CI `ctest --test-dir build --output-on-failure` 通过。
- 手动测试：
  - 打开 4K 60fps 视频不明显退化。
  - seek 多次不卡死。
  - pause/resume 不死锁。
  - native VideoToolbox path 可用时保持 native present。
  - native path 失败时 fallback 到 CPU path 且不会中断播放。

## 风险和控制

### 风险：再次引入播放卡顿

控制：

- 不改变 decode 线程模型。
- 不扩大 queue capacity 掩盖问题。
- 每阶段记录 runtime diagnostics。
- 4K60 本地视频作为手工验收项。

### 风险：Qt 线程越界

控制：

- SDK 不 include Qt。
- Qt presenter 继续内部 marshal。
- session 层只依赖 `IVideoPresenter`。

### 风险：fallback 语义错乱

控制：

- 先增加 session fallback 单测。
- fallback 统一在 session 层重建 core player。
- PlayPlugin 只收 notification。

### 风险：EOF 过早或过晚

控制：

- 明确 core EOF 和 runtime drained EOF 两种事件。
- 对外 UI EOF 只采用 runtime drained EOF。
- 增加 EOF ordering tests。

### 风险：音频设备环境差异

控制：

- 平台音频 contract 和 engine fake tests 并行。
- CI 保留 CTest diagnostics。
- no-audio policy 不隐式 fallback，必须有文档和测试。
