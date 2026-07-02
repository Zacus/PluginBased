# Media SDK B+ Frame Channel 设计文档

## 背景

当前 `media_sdk_core` 的 `DecodeWorker` 通过 `IEventSink::onEvent()` 同步输出所有 `PlayerEvent`。这条通道同时承载控制事件和音视频数据帧：

```text
DecodeWorker
    -> emitEvent(PlayerEvent)
    -> IEventSink::onEvent()
    -> SdkPlaybackAdapter::handleDataEvent()
    -> RuntimePlayer::enqueueAudio/enqueueVideo()
```

这个结构在功能上可用，但职责混杂。`IEventSink` 的实现者如果处理慢，`DecodeWorker` 就会被同步拖住。音视频帧进入 runtime 队列时的背压也隐藏在 `onEvent()` 调用里，调用者无法从接口上判断这是轻量控制事件、数据传输，还是可能阻塞的背压等待。

本设计是 B+ 的一次内部演进：不拆 demux/audio decode/video decode 多线程，只拆控制事件通道和帧数据通道，并把帧队列背压正式化。

## 决策

采用方案 B：

- `IEventSink` 只承载控制事件。
- 新增专用 `IDecodeFrameSink` 承载音视频帧。
- 允许破坏当前 `IEventSink` 的帧事件兼容，`DecodeWorker` 不再通过 `PlayerEvent` 发送 `AudioFrameEvent` 和 `VideoFrameEvent`。
- 背压结果从隐藏的同步调用改为结构化返回值。
- 维持单 `DecodeWorker` 线程模型，不拆 demux/audio decode/video decode 线程。

## 目标

1. 明确控制面和数据面的职责边界。
2. 保留数据帧背压，但背压必须显式、可取消、可观测。
3. 控制事件投递不能被音视频帧队列慢消费语义污染。
4. `seek/stop/close/generation change` 必须能取消阻塞中的 frame push。
5. `MediaInfoEvent` 必须作为 runtime/frame channel 初始化边界，避免帧先于 runtime 就绪。
6. 保持 `media_sdk_core` Qt-free，保持 `media_sdk_playback_runtime` Qt-free。
7. PlayPlugin 继续只负责展示和 Qt adapter，runtime 继续负责音频输出、A/V 队列、时钟同步和 presenter 背压。

## 非目标

1. 不拆分 demux thread、audio decode thread、video decode thread。
2. 不引入协程、全局事件总线、泛型 Observable 或 CRTP 主接口。
3. 不把 Qt RHI、QSG、QAudioSink、QObject 引入 SDK core 或 runtime。
4. 不保留 `AudioFrameEvent` / `VideoFrameEvent` 的旧发送路径作为长期兼容。
5. 不用增大队列、timer retry 或异步无限队列掩盖背压问题。
6. 不在本阶段实现 OpenGL presenter。

## 术语

- **控制事件**：播放状态、媒体信息、错误、seek 完成、position、EOF 等小对象事件。
- **帧数据**：`AudioFrame` 和 `VideoFrame`，可能携带大内存、native handle 或底层 FFmpeg storage 生命周期。
- **frame channel**：`DecodeWorker` 向下游推送帧数据的专用接口和队列协议。
- **背压**：下游队列已满或消费慢时，上游 decode 被限制推进的行为。
- **generation**：seek 后递增的代际，用于拒绝旧帧、旧 EOF 和旧完成事件。

## 架构约束

### 控制面

`IEventSink` 只允许发送以下事件：

- `MediaInfoEvent`
- `StateChangedEvent`
- `SeekCompletedEvent`
- `PositionChangedEvent`
- `ErrorEvent`
- `EndOfFileEvent`

控制事件要求：

- `onEvent()` 由 SDK worker/control thread 调用。
- `onEvent()` 必须快速返回，不能执行可能阻塞的数据帧入队、渲染等待、音频写入或文件 IO。
- Qt adapter 可以把 UI 更新通过 `Qt::QueuedConnection` 投递到 GUI thread。
- `MediaInfoEvent` 是例外边界：adapter 必须在返回前完成 runtime/frame sink 接收状态初始化，UI signal 仍可异步投递。

### 数据面

音视频帧不再经过 `IEventSink`。新增 `IDecodeFrameSink`：

```cpp
namespace media_sdk {

struct DecodeFrameMetadata {
    std::uint64_t sessionId = 0;
    std::uint64_t generation = 0;
};

enum class DecodeFramePushStatus {
    Accepted,
    Backpressured,
    StaleGeneration,
    Cancelled,
    Closed
};

struct DecodeFramePushResult {
    DecodeFramePushStatus status = DecodeFramePushStatus::Closed;
    std::chrono::microseconds waitTime { 0 };
};

class IDecodeFrameSink {
public:
    virtual ~IDecodeFrameSink() = default;

    [[nodiscard("frame push result determines whether decode can continue, retry, or stop")]]
    virtual DecodeFramePushResult pushAudio(AudioFrame frame,
                                            DecodeFrameMetadata metadata) = 0;

    [[nodiscard("frame push result determines whether decode can continue, retry, or stop")]]
    virtual DecodeFramePushResult pushVideo(VideoFrame frame,
                                            DecodeFrameMetadata metadata) = 0;
};

} // namespace media_sdk
```

`DecodeFramePushStatus` 语义：

- `Accepted`：帧已被下游接收，可以继续 decode。
- `Backpressured`：下游发生过等待但最终接收。用于诊断，不要求上游重试。
- `StaleGeneration`：帧属于旧 session/generation，必须丢弃且继续处理新命令。
- `Cancelled`：等待被 seek/stop/close/generation change 取消，worker 必须检查命令或退出。
- `Closed`：下游已关闭，worker 应停止推送当前帧流。

### Player 构造契约

`Player` 从单 sink 变为控制 sink + frame sink：

```cpp
class Player {
public:
    explicit Player(PlayerConfig config,
                    IEventSink& events,
                    IDecodeFrameSink& frames);
};
```

`PlaybackController` 和 `DecodeWorker` 同步接收这两个依赖。两个 sink 都是非 owning 引用，生命周期由 adapter/owner 保证长于 `Player`。`Player` 析构必须先停止 worker，再释放对 sink 的使用。

### DecodeWorker 行为

`DecodeWorker` 负责：

- demux/decode/seek/stop/pause 状态推进。
- 给控制事件打 `sessionId/generation`。
- 调用 `IDecodeFrameSink::pushAudio()` 和 `pushVideo()`。
- 根据 push 结果更新诊断和决定继续、丢弃、检查命令或退出。

`DecodeWorker` 不负责：

- runtime 队列的内部策略。
- audio clock、video wait/drop/render。
- Qt thread marshal。
- UI signal。

### MediaInfo 初始化边界

成功打开媒体后顺序必须是：

1. `DecodeWorker` 更新 `sessionId`、重置 `generation`。
2. `DecodeWorker` 发出 `MediaInfoEvent`。
3. `SdkPlaybackAdapter::onEvent(MediaInfoEvent)` 在 worker 线程同步完成 runtime 创建、timeline 建立、frame sink 接收状态打开。
4. `SdkPlaybackAdapter` 把 UI 的 `mediaInfoReady` 异步投递到 Qt GUI thread。
5. `DecodeWorker` 才允许在后续 play/decode 中推送音视频帧。

这个约束避免帧先到而 runtime 尚未创建。

### EOF 契约

本阶段 EOF 仍作为控制事件通过 `IEventSink` 发出，但它必须带 `sessionId/generation`。`SdkPlaybackAdapter` 收到 EOF 后调用 runtime 的 EOF/drain API，运行时必须保证 EOF marker 进入对应 generation 的顺序通道。

如果后续发现 EOF 仍需要和帧完全同通道，可以在下一阶段把 EOF 升级为 frame channel 的 drain marker。本阶段不同时改这个边界，避免扩大风险。

### 背压契约

背压必须满足：

1. 有明确容量上限。
2. 有明确返回状态。
3. 等待可被 seek/stop/close/generation change 唤醒。
4. 不阻塞 GUI thread。
5. 音频不能在正常播放中静默丢帧。
6. 视频可以在策略允许时丢旧帧或迟到帧，但必须由 runtime/scheduler 策略决定，并记录诊断。
7. 所有背压等待都必须有累计耗时、次数和高水位诊断。

### Runtime 入队契约

`RuntimePlayer::enqueueAudio()` 和 `enqueueVideo()` 从 `void` 改为返回结构化结果。runtime 可以定义内部结果类型，但 adapter 必须能映射到 `DecodeFramePushResult`。

建议：

```cpp
namespace media_sdk::runtime {

enum class RuntimeFramePushStatus {
    Accepted,
    Backpressured,
    RejectedGeneration,
    Cancelled,
    Closed
};

struct RuntimeFramePushResult {
    RuntimeFramePushStatus status = RuntimeFramePushStatus::Closed;
    std::chrono::microseconds waitTime { 0 };
};

} // namespace media_sdk::runtime
```

### 取消契约

以下操作必须取消阻塞中的 frame push：

- `Player::stop()`
- `Player` 析构
- `Player::open()` 触发旧媒体关闭
- `Player::seek()`
- runtime `stop()`
- runtime `seek()` / generation 切换
- presenter/device failure 导致 runtime fallback 或关闭

取消后，旧 generation 的 push 返回 `Cancelled` 或 `StaleGeneration`，不能在新 generation 中被接收。

## 组件职责

### `media_sdk_core`

- `Player.h`：公开 façade、`IEventSink`、`IDecodeFrameSink` 依赖。
- `MediaEvents.h`：控制事件 payload 和 metadata。
- `DecodeFrameSink.h`：帧通道接口和 push result。
- `DecodeWorker.h/.cpp`：调用控制 sink 和 frame sink。
- `DecodeDiagnostics.h` 或现有诊断结构：记录 frame push 等待和状态计数。

### `media_sdk_playback_runtime`

- `RuntimePlayer.h/.cpp`：返回 frame push result。
- `RuntimeFrameQueue.h`：提供可取消 push、abort、reset、generation 校验和高水位统计。
- `RuntimeTypes.h`：扩展诊断字段。

### `PlayPlugin`

- `SdkPlaybackAdapter`：实现 `IEventSink` 和 `IDecodeFrameSink`。
- `SdkPlaybackAdapter::onEvent(MediaInfoEvent)`：同步创建 runtime 和 frame sink 状态，异步投递 UI signal。
- `SdkPlaybackAdapter::pushAudio/Video`：调用 runtime 入队，映射结果。
- `QtPlaybackAdapter`：如果仍参与构建，必须实现 `IDecodeFrameSink` 或被移出当前播放路径；不能依赖旧帧事件。

## 诊断指标

新增或扩展以下指标：

- `decodeFramePushAccepted`
- `decodeFramePushBackpressured`
- `decodeFramePushStale`
- `decodeFramePushCancelled`
- `decodeFramePushClosed`
- `decodeFramePushWaitUs`
- `decodeFramePushMaxWaitUs`
- `audioQueueHighWatermark`
- `videoQueueHighWatermark`
- `videoDroppedByBackpressure`
- `audioBackpressureCount`
- `videoBackpressureCount`

日志输出应能回答：

- decode worker 是否被下游背压阻塞。
- 阻塞发生在音频还是视频。
- seek/stop 是否取消了阻塞等待。
- 队列是否长期接近满水位。
- 视频是否因为 backpressure policy 被丢弃。

## 测试策略

### SDK core 测试

- `IEventSink` 不再收到 `AudioFrameEvent` / `VideoFrameEvent`。
- `IDecodeFrameSink` 能收到音视频帧和正确 metadata。
- frame sink 返回 `StaleGeneration` 时 worker 丢弃旧帧并继续响应命令。
- frame sink 返回 `Cancelled` 时 worker 能及时处理 stop/seek。
- `MediaInfoEvent` 在首帧前发生。

### Runtime 测试

- 队列未满时 push 返回 `Accepted`。
- 队列满后 consumer 释放容量，push 返回 `Backpressured` 且记录 wait time。
- reset/abort/generation change 能唤醒等待中的 push。
- stop/close 后 push 返回 `Closed` 或 `Cancelled`。
- 高水位和等待时间诊断正确累计。

### PlayPlugin adapter 测试

- `SdkPlaybackAdapter` 同时实现 `IEventSink` 和 `IDecodeFrameSink`。
- `MediaInfoEvent` 同步完成 runtime 初始化。
- UI signal 仍走 Qt queued。
- frame push 不触碰 GUI thread 对象。
- 旧 generation frame 被拒绝。
- EOF 仍按当前 runtime drain API 进入对应 generation。

### 架构检查

- `DecodeWorker.cpp` 中不能出现 `makeEvent(AudioFrameEvent` 或 `makeEvent(VideoFrameEvent`。
- `IEventSink` 文档和检查必须说明 control-only。
- `media_sdk_core` 和 `media_sdk_playback_runtime` 禁止 Qt token。
- `RuntimePlayer::enqueueAudio/Video` 不允许返回 `void`。

## 风险和缓解

### 风险：MediaInfo 与首帧时序

如果 runtime 初始化仍依赖 Qt queued UI 事件，首帧可能早于 runtime 就绪。

缓解：`SdkPlaybackAdapter::onEvent(MediaInfoEvent)` 同步创建 runtime，UI signal 异步。

### 风险：背压取消不完整导致 seek/stop 卡住

队列满时 worker 可能等待容量，seek/stop 不能及时生效。

缓解：runtime queue 的 push 必须监听 abort/generation/closed 条件；seek/stop 先 abort/reset 队列，再唤醒等待。

### 风险：视频丢帧策略被 adapter 私自实现

adapter 为了不阻塞可能直接丢帧，破坏 AV sync。

缓解：adapter 只映射结果，不做播放策略。视频 backpressure drop 只能在 runtime/scheduler 明确策略中发生，并计入诊断。

### 风险：旧兼容路径残留

`AudioFrameEvent` / `VideoFrameEvent` 类型残留可能误导后续代码继续使用。

缓解：先停止发送并加架构检查；若没有外部 API 保留需求，后续删除 payload 类型。

## 分阶段交付

1. B+0：收口未接入代码和文档约束。
2. B+1：新增 `IDecodeFrameSink`，调整 `Player` 依赖。
3. B+2：迁移 `DecodeWorker` 帧输出路径。
4. B+3：runtime 入队返回结构化结果。
5. B+4：runtime queue 支持可取消背压和诊断。
6. B+5：PlayPlugin adapter 完成同步 runtime 初始化和结果映射。
7. B+6：测试、架构检查和手工播放验证收口。

每阶段独立提交。实现阶段每完成一个阶段后暂停，等待确认再进入下一阶段。
