# macOS SDK Playback Runtime 设计文档

## 背景

B+ 阶段已经把 demux、decode、frame contract、event generation、seek completion、EOF drain 和 Qt-free `media_sdk_core` 边界建立起来。但当前 PlayPlugin 接入 SDK 后，原有 macOS VideoToolbox native/近零拷贝路径被断开，视频帧退化为 SDK `VideoFrame` plane copy 到 Qt `AVFrame`，再上传到 QRhi 纹理。同时 A/V 队列、音频主时钟、视频 wait/drop/render 调度仍在 PlayPlugin 内部。

下一阶段目标是进入 C 阶段的第一步：新增 SDK playback runtime，让 SDK 管理音频输出、A/V 队列、主时钟、同步调度、背压、EOF drain 和 fallback；PlayPlugin 只保留 QML UI 和 Qt RHI/Scene Graph 展示入口。第一版只覆盖 macOS，优先恢复 VideoToolbox 零拷贝路径。

## 目标

1. 新增 `media_sdk_playback_runtime`，负责播放运行时状态机、AV 队列、背压、A/V sync、EOF drain 和 seek/pause/stop 生命周期。
2. 新增 macOS `CoreAudioAudioOutput`，由 SDK runtime 直接管理音频输出和音频主时钟。
3. 保留 PlayPlugin 的 Qt RHI/`FFmpegSurface` 展示能力，但将视频调度权迁入 SDK runtime。
4. 恢复 macOS VideoToolbox native zero-copy/near-zero-copy 路径：`CVPixelBufferRef -> CVMetalTexture -> QRhiTexture`。
5. 使用纯 C++ SDK presenter 接口，Qt presenter 实现内部自行 marshal 到 Qt GUI/Scene Graph 合法线程。
6. 新旧播放链路分阶段并行接入，通过开关切换，便于 A/B 对比和回滚。
7. 对 native/cpu 路径、队列、drop/wait、fallback 和 EOF drain 提供诊断计数，证明性能路径没有再次退化。

## 非目标

1. 第一版不做 Windows D3D11、Linux VAAPI、OpenGL presenter。
2. 第一版不把 Qt/QML/QRhi 类型放入 `media_sdk_core` 或 SDK 公共 core header。
3. 第一版不直接让 SDK 创建或拥有 Qt `QQuickItem`、`QSGNode`、`QRhi`。
4. 第一版不删除旧 PlayPlugin 播放链路；新旧链路先共存。
5. 第一版不承诺第三方稳定二进制 ABI；先稳定源码级 C++20 API 和生命周期契约。

## 总体架构

```text
media_sdk_core
  Demuxer / StreamDecoder / VideoFrameProcessor / HardwareDecoderBackend
  MediaInfo / AudioFrame / VideoFrame / NativeHandle / PlayerEvent

media_sdk_playback_runtime
  RuntimePlayer
  PlaybackStateMachine
  AudioFrameQueue / VideoFrameQueue
  BackpressurePolicy
  MasterClock
  AvSyncScheduler
  EofDrainCoordinator
  NativeFallbackController

media_sdk_platform_audio_macos
  CoreAudioAudioOutput

PlayPlugin
  QML UI
  PlayerEngine Qt facade
  QtRhiVideoPresenter
  FFmpegSurface / VideoNode / VideoMaterial
  AppleMetalVideoTextureBridge
```

依赖方向：

```text
PlayPlugin Qt presenter -> media_sdk_playback_runtime -> media_sdk_core
CoreAudioAudioOutput -> media_sdk_playback_runtime interfaces
media_sdk_core 不依赖 runtime、Qt 或 CoreAudio presenter
```

`media_sdk_playback_runtime` 只依赖 `IAudioOutput`、`IVideoPresenter` 等接口，不直接 include 或创建 `CoreAudioAudioOutput`。具体平台实现由应用 composition root 或 factory 注入：

```text
PlayPlugin / app composition root
  creates CoreAudioAudioOutput
  creates QtRhiVideoPresenter
  injects both into RuntimePlayer
```

## 分层职责

### media_sdk_core

Core 继续只负责：

- 打开媒体、探测流、demux、decode。
- VideoToolbox backend 和 native handle 描述。
- 音视频 frame contract。
- seek/open/stop command submission 和 worker event 输出。
- timestamp normalization 和 decode diagnostics。

Core 不负责：

- 音频设备输出。
- A/V master clock。
- 视频 wait/drop/render 决策。
- Qt RHI、Metal layer、OpenGL 或 UI 展示。

### media_sdk_playback_runtime

Runtime 负责：

- 拥有 `media_sdk::Player` 或内部 core playback controller。
- 接收 core event，并写入 runtime AV 队列。
- 管理 audio/video queue capacity、blocking backpressure、abort、flush、serial/generation。
- 通过注入的 `IAudioOutput` 管理音频写入和音频主时钟。
- 根据 master clock 决定视频帧 `Wait`、`Render`、`Drop`。
- 调用 `IVideoPresenter` 展示到点的视频帧。
- 管理 seek/pause/resume/stop/open/EOF drain。
- 管理当前 session 的 native/cpu output policy 和 fallback。

Runtime 不直接访问 QObject、QQuickItem、QSGNode 或 QRhi。

### PlayPlugin

PlayPlugin 目标职责：

- QML UI、播放控制按钮、播放列表、错误展示。
- 创建 Qt presenter 并传给 SDK runtime。
- `QtRhiVideoPresenter` 将 SDK frame 展示到 `FFmpegSurface`。
- Qt presenter 内部负责线程切换。

最终 PlayPlugin 不再直接拥有：

- 音频输出线程。
- AV 帧队列背压策略。
- 音频主时钟。
- 视频调度器。
- EOF 三路完成协调器。

## 核心接口草案

### Audio Output

```cpp
struct AudioFormat {
    int sampleRate = 0;
    int channels = 0;
    AudioSampleFormat sampleFormat = AudioSampleFormat::Unknown;
};

struct ClockSnapshot {
    std::chrono::microseconds position { 0 };
    std::chrono::microseconds hardwareLatency { 0 };
    std::chrono::microseconds queuedDuration { 0 };
    std::uint64_t generation = 0;
    bool valid = false;
    bool paused = false;
};

class IAudioOutput {
public:
    virtual ~IAudioOutput() = default;

    virtual Result<void> open(const AudioFormat& format) = 0;
    virtual Result<void> write(AudioBufferView buffer) = 0;
    virtual ClockSnapshot clock() const = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void flush() = 0;
    virtual void close() = 0;
};
```

`CoreAudioAudioOutput` 实现 `IAudioOutput`，但由 composition root 注入 runtime。它负责 AudioUnit/AudioQueue 初始化、音频设备写入、pause/resume/flush 和真实播放进度 clock。

CoreAudio clock 约束：

- `clock().position` 表示估算的实际硬件播放位置，不是已经写入 CoreAudio 的字节位置。
- 实现必须考虑 device sample time、host time、已排队帧数、hardware latency 和 pause/flush 后的基准重置。
- 如果采用 CoreAudio pull callback，`write()` 可以表示向 SDK runtime 内部 audio ring buffer 提交数据；callback 消费后更新 device clock snapshot。
- `flush()` 必须递增 audio clock generation，使 runtime 能丢弃 flush 前的 pending audio/video sync 决策。

### Video Presenter

```cpp
struct VideoPresenterCapabilities {
    bool supportsVideoToolboxPixelBuffer = false;
    bool supportsCpuYuv = true;
    bool asyncPresent = true;
};

enum class PresentStatus {
    Presented,
    Queued,
    UnsupportedNativeHandle,
    DeviceLost,
    Failed
};

struct PresentTiming {
    std::chrono::microseconds pts { 0 };
    std::chrono::microseconds clock { 0 };
    std::chrono::microseconds lateness { 0 };
};

using PresentId = std::uint64_t;

struct PresentResult {
    PresentId id = 0;
    PresentStatus status = PresentStatus::Failed;
};

struct PresentCompletion {
    PresentId id = 0;
    PresentStatus status = PresentStatus::Failed;
    std::string detail;
};

class IVideoPresenterEvents {
public:
    virtual ~IVideoPresenterEvents() = default;
    virtual void onPresentComplete(PresentCompletion completion) = 0;
};

class IVideoPresenter {
public:
    virtual ~IVideoPresenter() = default;

    virtual VideoPresenterCapabilities capabilities() const = 0;
    virtual void setEvents(IVideoPresenterEvents* events) = 0;
    virtual PresentResult present(VideoFrame frame, PresentTiming timing) = 0;
    virtual void clear() = 0;
};
```

`QtRhiVideoPresenter` 实现 `IVideoPresenter`，但 SDK runtime 不知道 QObject。Qt presenter 内部用 `QMetaObject::invokeMethod()` 或等价机制 marshal 到 GUI 线程，再由 `FFmpegSurface` 和 Scene Graph 完成渲染。

异步完成规则：

- `present()` 返回 `Queued` 只表示 presenter 接收了提交，不表示 native texture 已创建或已经绘制。
- native texture 创建失败、设备丢失或 Qt surface 销毁必须通过 `IVideoPresenterEvents::onPresentComplete()` 回传给 runtime。
- `PresentId` 由 presenter 或 runtime 保证单调唯一，用于忽略 stop/open/seek 后迟到的 completion。
- runtime 必须把 completion 与 session/generation 绑定；旧 generation 的失败不能触发当前 session fallback。

## 线程模型

```text
SDK decode worker thread
  demux/decode -> PlayerEvent

SDK runtime thread(s)
  event ingest
  AV queue
  audio write
  master clock
  video scheduling
  EOF drain

CoreAudio callback / audio device thread
  audio consumption and device clock

Qt GUI thread
  QML / PlayerEngine / FFmpegSurface state

Qt Scene Graph render thread
  VideoNode / QRhi texture binding / shader draw
```

线程规则：

- SDK runtime 可以阻塞在自己的队列和 CoreAudio 写入路径，不能阻塞 Qt GUI 线程。
- `IVideoPresenter::present()` 可以是异步提交；Qt presenter 必须内部保证 QObject 线程亲和性。
- Runtime 不阻塞等待 Qt Scene Graph draw，但必须处理 presenter completion/failure callback。
- stop/open/seek 必须 abort runtime queue、唤醒 audio/video 调度和取消 pending present。

## VideoToolbox 零拷贝路径

目标路径：

```text
VideoToolbox decoder
  -> AVFrame(format=AV_PIX_FMT_VIDEOTOOLBOX)
  -> CVPixelBufferRef
  -> SDK VideoFrame {
       pixelFormat = Native,
       nativeHandle.kind = VideoToolboxPixelBuffer,
       storage owns AVFrame/CVPixelBuffer lifetime
     }
  -> runtime video queue
  -> AvSyncScheduler decides Render
  -> QtRhiVideoPresenter::present(VideoFrame)
  -> AppleMetalVideoTextureBridge
  -> CVMetalTextureCacheCreateTextureFromImage
  -> QRhiTexture::createFrom(native Metal texture)
  -> shader YUV->RGB
```

禁止路径：

```text
Native frame -> adapter 丢弃
Native frame -> CPU plane memcpy
Native frame -> sws_scale RGB
```

Native handle 生命周期：

- `VideoFrame::storage` 必须持有能保证 `CVPixelBufferRef` 存活的所有权。
- 如果 storage 持有 `AVFrame`，则 `AVFrame` 释放前必须保证 render 侧不再使用 `data[3]`。
- Qt presenter 不拥有底层 CVPixelBuffer；只在 `VideoFrame` 生命周期内借用。
- 如果 Qt presenter 异步持有 frame，必须保存 `VideoFrame` 值或其 shared storage，不能只保存裸 handle。
- `CVMetalTextureRef` 必须由 presenter 侧 RAII 对象持有，并至少活到对应 QRhi native texture 不再被 Scene Graph 使用。
- `QRhiTexture::createFrom()` 创建的 native wrapper 与 `CVMetalTextureRef` 释放顺序必须在 presenter 中固定：先停止使用/替换 QRhiTexture，再释放 CVMetalTextureRef。
- `presenter.clear()`、stop、surface 销毁和 device lost 必须取消 pending present，并释放 presenter 持有的 `VideoFrame`、CVMetalTexture 和 QRhiTexture。
- 任何 render-thread 持有的 native texture 集合都必须由 Qt presenter 管理，不能由 SDK runtime 直接释放。

## Fallback 策略

已确认策略：

```text
当前文件 native 失败 -> 当前 session 禁用 native，fallback 到 CPU YUV
下一次 open -> 重新根据 Surface/Metal 能力探测 native
```

流程：

1. `QtRhiVideoPresenter` 通过同步 `PresentResult` 或异步 `PresentCompletion` 报告 `UnsupportedNativeHandle`、`DeviceLost` 或 `Failed`。
2. runtime 校验 completion 的 session/generation；旧 completion 只计数，不触发当前 session fallback。
3. runtime 进入 `FallbackPending` 状态，暂停视频调度，并让 audio output pause。
4. runtime 记录 `nativeFallbacks++`，把当前 session 的 output policy 切为 `CpuOnly`。
5. runtime abort audio/video queue，flush CoreAudio buffered audio，清空 pending native frames 和 pending present。
6. runtime 递增 seek/fallback generation，使 fallback 前的 audio/video/EOF/completion 全部失效。
7. runtime 请求 core 切换 `preferNativeVideoFrames=false`。
8. runtime 按 fallback generation 从当前 audio clock 对应 position 附近重新 seek 或重新解码。
9. 新 generation 的 `SeekCompletedEvent` 到达后，runtime resume audio output 和 video scheduling。
10. UI 不报 fatal error，播放继续；日志必须记录 fallback 原因。

第一版允许 fallback seek 粒度为当前 audio clock 对应 position，后续可优化为更精确的 decoder resume。

Fallback 是完整 runtime state transition，不能只 abort video queue。它必须同时处理 audio output、audio queue、video queue、pending present、generation 和 EOF drain 状态。

## Output Policy

```cpp
enum class VideoOutputPolicy {
    PreferNative,
    CpuOnly,
    RequireNative
};
```

第一版使用：

```text
PreferNative
```

策略来源：

- macOS 平台。
- VideoToolbox backend 可用。
- Qt presenter capability 支持 `VideoToolboxPixelBuffer`。
- 当前 session 未发生 native fallback。
- 用户或配置未禁用 native。

## AV 队列与背压

Runtime 内部队列负责：

- audio queue：正常播放不能静默丢帧。
- video queue：不能在 ingest 阶段无条件丢帧；只允许 scheduler 根据 lateness 丢帧。
- EOF marker：必须排在同一 generation 已接受 frame 之后。
- seek/open/stop：必须 abort 队列并使旧 generation frame 无效。

队列元素不再是 Qt `AVFramePtr`，而是 SDK frame contract：

```cpp
struct RuntimeVideoFrame {
    VideoFrame frame;
    std::uint64_t sessionId = 0;
    std::uint64_t generation = 0;
};
```

PlayPlugin 的旧 `FrameQueue<AVFramePtr>` 只在旧链路保留。新链路不得依赖 Qt-side AVFrame queue。

## A/V 同步策略

第一版 master clock：

```text
CoreAudioAudioOutput::clock()
```

当无音频流时：

```text
runtime monotonic video clock
```

调度决策：

```cpp
enum class VideoScheduleAction {
    Render,
    Wait,
    Drop
};
```

规则：

- frame PTS 比 master clock 早太多：drop，计入 `videoDroppedLate`。
- frame PTS 还未到：wait 到目标显示时间附近。
- frame PTS 在阈值内：present。
- 连续 drop 超过阈值时强制 render 一帧，避免长时间无画面。
- presenter 已有 pending frame 时，新 frame 的处理由 runtime policy 决定：同 generation 且更接近当前 clock 的 frame 可以替换旧 pending frame；跨 generation pending frame 必须取消。

具体阈值第一版沿用当前 PlayPlugin 行为：

- submit lead time：约 2 ms。
- late drop threshold：约 100 ms。
- max scheduled wait：约 40 ms。
- max consecutive drops before forced render：8。

Qt presenter 延迟约束：

- 2 ms submit lead time 只作为初始值，runtime 必须记录 presenter queue latency 和 present completion latency。
- 如果 Qt marshal + render loop 延迟稳定大于 submit lead time，应通过配置或自适应估计提高 lead time。
- runtime 需要限制 presenter pending depth，避免 Qt GUI 线程积压过期帧。

## 新旧链路并行接入

短期保留旧链路：

```text
QtPlaybackAdapter + PlaybackDataBridge + AudioRenderer + VideoRenderer + FrameQueue
```

新增新链路：

```text
RuntimePlayer + CoreAudioAudioOutput + QtRhiVideoPresenter
```

切换方式：

- 增加运行时配置或实验开关，例如 `PlayerEngine::setPlaybackRuntimeMode(...)`。
- 播放期间切换模式必须先执行 stop/close 旧链路，等待旧 audio output、decode worker、video presenter 和 pending present 完全停止。
- 同一时刻只允许一条链路拥有音频设备。
- 同一时刻只允许一条链路绑定同一个 `FFmpegSurface`。
- 切换模式必须清空 surface、completion tracker、队列、runtime/pipeline 状态和错误状态。
- 默认先保持旧链路，便于稳定回归。
- CI 和手工测试覆盖两条链路。
- 新链路稳定后再改默认值，最后移除旧链路。

## 诊断指标

Runtime 需要输出 session 级汇总：

```text
nativeDecoded
nativeAccepted
nativePresented
nativeFallbacks
nativeTextureCreated
nativeTextureFailed
nativeTextureDrawn
cpuDecoded
cpuPresented
cpuCopied
cpuTransferred
cpuMemcpy
hardwareTransfers
audioQueued
audioWritten
videoQueued
videoWaited
videoDroppedLate
videoPresented
eofAccepted
eofPresented
queueAbortCount
```

零拷贝成功的关键判断：

```text
nativePresented > 0
nativeTextureCreated > 0
nativeTextureDrawn > 0
cpuCopied == 0
cpuTransferred == 0
cpuMemcpy == 0
nativeFallbacks == 0
```

`nativePresented` 只表示 runtime/presenter 接收了 native frame；`nativeTextureCreated` 和 `nativeTextureDrawn` 用于证明 Qt/Metal 路径实际创建并使用 native texture。`cpuTransferred`、`cpuMemcpy` 用于证明没有发生硬件帧回读或 plane copy。

如果 fallback 发生：

```text
nativeFallbacks > 0
cpuPresented > 0
播放不中断
下一次 open 重新探测 native
```

## 测试策略

### 架构检查

- `media_sdk_core` 不包含 Qt/QML/QRhi/QSG/QAudio/CoreAudio presenter 类型。
- `media_sdk_playback_runtime` 不包含 QObject/QQuickItem/QSGNode。
- `QtRhiVideoPresenter` 是唯一允许依赖 Qt RHI/Scene Graph 的新 presenter。
- 新链路不得把 `PixelFormat::Native` 丢弃。
- 新链路不得把 `preferNativeVideoFrames` 固定为 false。
- 新链路不得依赖 Qt `FrameQueue<AVFramePtr>`。
- `media_sdk_playback_runtime` 不得直接 include `CoreAudioAudioOutput` 具体实现头；只能依赖 `IAudioOutput`。
- `IVideoPresenter` 必须提供异步 completion/failure 回传通道。
- 新旧链路切换必须经过 stop/close 互斥状态机，不能同时拥有音频设备或同一 surface。

### 单元测试

- runtime queue push/pop/abort/finish/generation filter。
- A/V scheduler wait/drop/render 决策。
- EOF drain 顺序。
- native fallback state machine。
- CoreAudioAudioOutput mock clock 驱动的视频调度。
- presenter async completion：queued、success、failure、迟到旧 generation completion。
- fallback transition 同时 flush audio/video queue、audio output 和 pending present。

### 集成测试

- 使用 mock `IAudioOutput` 和 mock `IVideoPresenter` 验证 runtime 不依赖 Qt。
- 验证 seek 后旧 generation frame 不会 present。
- 验证 native present 失败后切换 CPU policy 并继续播放。
- 验证无音频流时使用 video monotonic clock。
- 验证新旧链路切换不会双开音频设备，也不会双写同一 surface。
- 验证 Qt presenter native texture 创建失败通过 async completion 触发 fallback。

### 手工验证

macOS 真机：

- VideoToolbox native path 有画面。
- 日志显示 `nativePresented > 0` 且 `cpuCopied == 0`。
- 日志显示 `nativeTextureCreated > 0`、`nativeTextureDrawn > 0`，且 `cpuTransferred == 0`、`cpuMemcpy == 0`。
- native path 失败可 fallback，不黑屏。
- pause/play 不冻结。
- seek 前后没有旧帧。
- EOF 在音视频 drain 后触发。
- 播放性能不低于 B+ 迁移前 native path。

## 实施阶段建议

1. 建立 `media_sdk_playback_runtime` target、接口和架构检查，runtime 只依赖注入的 `IAudioOutput` / `IVideoPresenter`。
2. 增加 runtime queue、generation filter、EOF drain 和测试。
3. 增加 scheduler 和 mock clock 测试。
4. 增加 `IAudioOutput` 和 mock audio output，runtime 使用 audio clock 调度。
5. 实现 `CoreAudioAudioOutput`，先用 mock presenter 验证音频主时钟。
6. 定义 `IVideoPresenter`、completion callback 和 `QtRhiVideoPresenter`。
7. 打通 VideoToolbox native frame 到 Qt presenter。
8. 实现 native failure fallback 到 CPU YUV。
9. 增加新旧链路切换开关。
10. 手工验证后再考虑把新 runtime 设为默认。

## 风险与约束

- CoreAudio 引入平台复杂度，必须用 RAII 管理 AudioUnit/AudioQueue 资源。
- Qt presenter 异步展示必须持有 `VideoFrame` storage，不能保存裸 native handle。
- fallback 需要清空 native 队列并重建 CPU 输出策略，否则会出现连续失败或黑屏。
- 新旧链路共存期间要防止两套播放器同时持有音频设备。
- 新旧链路共存期间要防止两套 presenter 同时写同一个 `FFmpegSurface`。
- Qt presenter completion 可能在 stop/open/seek 后迟到，runtime 必须用 session/generation/present id 过滤。
- CoreAudio clock 必须是硬件消费进度估计，不是写入进度；否则 A/V sync 会系统性偏移。
- runtime 不能直接访问 Qt 对象，否则会破坏 SDK 可复用性。
- 所有跨线程回调必须有明确生命周期，stop/open/seek 必须先取消阻塞，再销毁对象。
