# Media SDK B+ 设计文档

## 背景

`PlayPlugin` 现在已经把播放器拆成 `PlayerEngine`、`PlaybackPipeline`、`FFmpegDecoder`、`AudioRenderer`、`VideoRenderer`、`FFmpegSurface` 和若干 helper。这个结构适合 Qt/QML 插件内部维护，但编解码和播放内核仍然直接依赖 Qt 类型、Qt 线程模型、Qt 信号槽和 Qt RHI/Scene Graph 渲染路径。

下一阶段目标是把播放器演进成可复用 SDK：先抽出 Qt-free 的 C++20 播放内核，再让当前 PlayPlugin 通过 Qt adapter 接入该 SDK。这个阶段称为 B+。B+ 不把 Qt RHI 封装进 core SDK，也不急于完成 OpenGL presenter；它先稳定 media core、frame contract 和 adapter 边界，为后续 C 阶段的 presenter 插件化留下清晰扩展点。

## 目标

1. 新增纯 C++20 `media_sdk_core`，不依赖 Qt、QML、QRhi、QSG、QAudioSink 或 QObject。
2. 把 demux、decode、seek、pause/stop、frame queue、clock、A/V sync、视频帧调度收敛到 SDK core。
3. 用 SDK 自有类型描述 media info、audio frame、video frame、错误、事件和播放命令。
4. 当前 Qt PlayPlugin 保持用户可见行为和 QML API 稳定，通过 adapter 调用 SDK。
5. Qt RHI 渲染保留在 Qt adapter 层，core 只输出可渲染 frame contract。
6. 保留后续演进到 C 阶段的空间：`IAudioSink`、`IVideoPresenter`、OpenGL/Qt RHI presenter 可以在 core 稳定后接入。
7. 使用 C++20，遵循 SOLID、RAII、依赖倒置、接口隔离、单向依赖和可测试性原则。

## 非目标

1. B+ 阶段不实现 OpenGL presenter。
2. B+ 阶段不把 `FFmpegSurface`、`VideoNode`、`VideoMaterial` 移入 SDK core。
3. B+ 阶段不承诺二进制 ABI 给第三方；先稳定源码级 C++ API。若后续需要第三方长期集成，再设计 C ABI 或 PIMPL ABI。
4. B+ 阶段不改变 `IAppPlugin` ABI。
5. B+ 阶段不重写 FFmpeg 解码策略，只迁移和收束边界。
6. B+ 阶段不引入协程、全局单例播放器或跨 UI 框架的渲染抽象实现。

## 设计原则

### 依赖规则

依赖只能从外层指向内层：

```text
PlayPlugin QML/UI
    -> Qt adapter
        -> media_sdk_core
            -> FFmpeg/platform codec wrappers
```

`media_sdk_core` 不能 include Qt 头文件。Qt adapter 可以 include SDK 头文件和 Qt 头文件。渲染后端可以依赖 Qt RHI 或 OpenGL，但不能让这些类型进入 SDK core API。

### SOLID 落地方式

- 单一职责：demux、decode、queue、clock、scheduler、command state、event delivery 分成小对象。
- 开闭原则：新增 Qt RHI/OpenGL presenter 时扩展 adapter，不修改 core 播放状态机。
- 里氏替换：adapter 接口只表达能力和契约，不要求实现方继承不适用行为。
- 接口隔离：event sink、audio sink、video frame sink、logger、hardware policy 分开定义。
- 依赖倒置：core 依赖抽象 callback/sink，Qt 依赖 core 并实现 adapter。

### C++20 工程规则

- 使用 `std::jthread`、`std::stop_token`、`std::mutex`、`std::condition_variable_any`、`std::atomic`、`std::chrono::steady_clock` 替代 Qt 线程和时间工具。
- 使用 RAII 包装 FFmpeg 资源，禁止裸 owning pointer。
- SDK 对外数据结构优先使用值类型、`std::string`、`std::filesystem::path`、`std::span`、`std::chrono`。
- 跨线程回调必须说明调用线程。core 默认从 worker thread 发出事件，Qt adapter 负责 marshal 到 GUI thread。
- 对外错误使用结构化 `MediaError` / `Result<T>`，不要只返回 bool 加日志。
- SDK 头文件不暴露 Qt 类型；尽量少暴露 FFmpeg 类型。需要硬件零拷贝时，通过 `NativeHandle` 描述符表达。

## 目标架构

### 1. media_sdk_core

建议新建：

```text
sdk/media_core/
  include/media_sdk/
    Player.h
    PlayerConfig.h
    MediaTypes.h
    MediaEvents.h
    Frame.h
    Sinks.h
    Error.h
  src/
    Player.cpp
    PlaybackController.cpp
    DecodeWorker.cpp
    Demuxer.cpp
    StreamDecoder.cpp
    VideoFrameProcessor.cpp
    AudioFrameProcessor.cpp
    FrameQueue.h
    ClockSync.cpp
    FrameScheduler.cpp
    ffmpeg/FFmpegUtils.h
    hw/HardwareDecoderBackend.h
```

Core 负责：

- 打开媒体、探测流、创建 decoder。
- 解码音视频 frame。
- 管理 seek/pause/resume/stop。
- 管理 audio/video frame queue。
- 维护主时钟和 video frame scheduling。
- 输出 media info、state、error、audio frame、video frame、EOF 等事件。

Core 不负责：

- Qt signal/slot。
- QML 属性。
- QAudioSink。
- QRhi/QSG/OpenGL 纹理创建。
- UI thread marshal。

### 2. SDK 公共 API

核心 API 保持小而稳定：

```cpp
namespace media_sdk {

struct PlayerConfig {
    bool enableHardwareDecode = true;
    bool preferNativeVideoFrames = true;
    int videoQueueCapacity = 30;
    int audioQueueCapacity = 64;
};

class IEventSink {
public:
    virtual ~IEventSink() = default;
    virtual void onEvent(const PlayerEvent& event) = 0;
};

class Player {
public:
    explicit Player(PlayerConfig config, IEventSink& events);
    ~Player();

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    Result<void> open(const std::filesystem::path& path);
    void play();
    void pause();
    void stop();
    Result<void> seek(std::chrono::milliseconds position);
    void setVolume(float volume);
    void setMuted(bool muted);
};

}
```

说明：

- `Player` 是 SDK façade，但不是全能类。内部委托 `PlaybackController`、`DecodeWorker`、queue、clock、scheduler。
- `IEventSink` 是核心事件出口。Qt adapter 实现它，把事件投递到 Qt GUI 线程后转成 signals。
- B+ 阶段 `setVolume`/`setMuted` 可以先由 Qt adapter 处理；如果 core 不直接输出音频，应避免在 core API 中承诺音量行为。最终 API 以实施计划确认。

### 3. Frame Contract

SDK 不应把 `AVFrame*` 作为主要外部契约。推荐定义：

```cpp
enum class PixelFormat {
    Yuv420P,
    Nv12,
    P010,
    Yuv420P10,
    Yuv422P10,
    Yuv444P10,
    Native
};

enum class ColorRange {
    Unknown,
    Limited,
    Full
};

enum class ColorSpace {
    Unknown,
    Bt601,
    Bt709
};

enum class NativeHandleKind {
    None,
    VideoToolboxPixelBuffer,
    D3D11Texture,
    VaapiSurface
};

struct PlaneView {
    const std::byte* data = nullptr;
    int stride = 0;
    int width = 0;
    int height = 0;
};

struct NativeHandle {
    NativeHandleKind kind = NativeHandleKind::None;
    void* handle = nullptr;
    int pixelFormat = 0;
};

class VideoFrame {
public:
    int width() const;
    int height() const;
    PixelFormat pixelFormat() const;
    ColorRange colorRange() const;
    ColorSpace colorSpace() const;
    std::chrono::microseconds pts() const;
    std::span<const PlaneView> planes() const;
    NativeHandle nativeHandle() const;

private:
    std::shared_ptr<void> storage_;
};
```

关键点：

- `VideoFrame` 持有 shared storage，确保 Qt/渲染线程消费期间底层 AVFrame 或 native handle 不被释放。
- CPU frame 通过 plane views 暴露只读数据。
- 硬件 frame 通过 `NativeHandle` 暴露描述符；具体解释由 adapter/presenter 负责。
- Core 内部可继续使用 FFmpeg `AVFrame`，但 API 使用 SDK frame contract。

### 4. Qt Adapter

建议新建或保留在 PlayPlugin 内：

```text
plugins/PlayPlugin/src/sdk_adapter/
  QtPlaybackAdapter.h/.cpp
  QtFrameMapper.h/.cpp
  QtAudioSinkAdapter.h/.cpp
  QtVideoFrameBridge.h/.cpp
```

职责：

- 把 `QUrl` / `QString` 转成 `std::filesystem::path` / `std::string`。
- 持有 `media_sdk::Player`。
- 实现 `media_sdk::IEventSink`。
- 把 SDK worker thread event 投递到 Qt GUI thread。
- 把 SDK `VideoFrame` 转换为现有 `VideoFrameDataPtr` 或新的 Qt-facing frame wrapper。
- 维持现有 `PlayerEngine` QML 属性和信号。
- 在 B+ 阶段继续使用现有 `FFmpegSurface`、`VideoNode`、`VideoMaterial` 和 Qt RHI 路径。

### 5. Audio 边界

B+ 有两个可选切法：

推荐第一步：core 输出 decoded/resampled audio frames，Qt adapter 用 `QAudioSink` 播放并回写音频时钟快照给 core。

更完整但风险更高：core 内部管理 audio clock，并通过 `IAudioOutput` 抽象写音频设备。

B+ 推荐第一步采用前者，因为当前 `QAudioSink` 线程亲和性强，直接迁进 SDK 会让 core 被 Qt 约束。后续 C 阶段再抽 `IAudioSink`，让 CoreAudio/WASAPI/ALSA/QtAudio 都作为 adapter。

### 6. Video 边界

B+ 阶段 core 负责视频帧是否应该 render/wait/drop，并发出 ready-to-present 的 `VideoFrame`。Qt adapter 接收后交给现有 surface。

后续 C 阶段再引入：

```cpp
class IVideoPresenter {
public:
    virtual ~IVideoPresenter() = default;
    virtual PresenterCapabilities capabilities() const = 0;
    virtual PresentResult present(const VideoFrame& frame) = 0;
};
```

这能让 `QtRhiPresenter` 和 `OpenGLPresenter` 平行存在，但它们不进入 core SDK。

## 当前代码迁移映射

| 当前模块 | B+ 目标位置 | 处理方式 |
| --- | --- | --- |
| `common/FFmpegUtils.h` | `sdk/media_core/src/ffmpeg/` | 基本迁移，去除 Qt helper。 |
| `common/FrameQueue.h` | `sdk/media_core/src/FrameQueue.h` | 改为 C++20 mutex/condition_variable/callback。 |
| `sync/ClockSync.h` | `sdk/media_core/src/ClockSync.*` | 改为 chrono/atomic/std mutex。 |
| `video/VideoFrameScheduler.*` | `sdk/media_core/src/FrameScheduler.*` | 基本迁移，去掉 `QtGlobal`。 |
| `decode/MediaOpener.*` | `sdk/media_core/src/Demuxer.*` | `QString` 改 `std::filesystem::path` / `std::string`。 |
| `decode/StreamFrameDecoder.*` | `sdk/media_core/src/StreamDecoder.*` | 基本迁移。 |
| `decode/VideoFrameProcessor.*` | `sdk/media_core/src/VideoFrameProcessor.*` | 去掉 `QElapsedTimer`，输出 SDK frame metadata。 |
| `decode/DecodePerformance.*` | `sdk/media_core/src/Diagnostics.*` | `QString` 改 `std::string`，日志由接口注入。 |
| `decode/FFmpegDecoder.*` | `sdk/media_core/src/DecodeWorker.*` | `QThread/signals` 改 `std::jthread` 和 event sink。 |
| `audio/AudioRenderer.*` | Qt adapter 暂留 | B+ 不把 QAudioSink 放入 core。 |
| `video/VideoRenderer.*` | 部分进入 core | 调度逻辑进 core，Qt timer/surface 交付留 adapter。 |
| `video/FFmpegSurface.*` / `video/render/*` | Qt adapter 暂留 | B+ 不迁移。 |
| `PlaybackPipeline.*` | Qt adapter + SDK facade | 逐步瘦身为 Qt adapter orchestration。 |
| `PlayerEngine.*` | Qt QML facade | QML API 不变，内部改用 Qt adapter。 |

## 线程和生命周期

Core 内部建议：

- `Player` 拥有 `PlaybackController`。
- `PlaybackController` 拥有 `DecodeWorker`、queues、clock、scheduler 和 command state。
- `DecodeWorker` 使用 `std::jthread`，析构时 request stop 并 join。
- 队列支持 `abort()`，stop/seek/open 必须确定唤醒所有阻塞等待。
- Event sink 只在 worker/control thread 调用，不假设 GUI thread。
- Qt adapter 持有 `media_sdk::Player`，析构顺序为：停止 SDK player、断开 Qt-facing surface/audio sink、释放 player。

Native frame 生命周期：

- CPU frame storage 由 SDK `VideoFrame` shared storage 持有。
- VideoToolbox/D3D/VAAPI handle 的引用计数或 release 逻辑由 SDK frame storage RAII 管理。
- Qt RHI adapter 只借用 `VideoFrame` 生命周期内的 native handle，不拥有底层资源。

## 错误处理

Core 统一错误结构：

```cpp
enum class MediaErrorCode {
    OpenFailed,
    StreamInfoFailed,
    DecoderOpenFailed,
    DecodeFailed,
    SeekFailed,
    UnsupportedFormat,
    InternalStateError
};

struct MediaError {
    MediaErrorCode code;
    std::string message;
    std::string detail;
};
```

规则：

- 边界函数返回 `Result<T>`，异步错误通过 `PlayerEvent::Error`。
- 错误消息必须包含操作、文件路径或 stream 信息。
- 硬解失败可以 fallback，但必须产生 diagnostic event 或日志。
- bad state 不能静默忽略，例如未 open 时 seek 应返回结构化错误或 no-op event，具体行为在 API 文档固定。

## 测试策略

新增测试分三类：

1. Core unit tests：
   - `FrameScheduler` wait/drop/render。
   - `ClockSync` pause/resume/invalid clock。
   - `FrameQueue` push/pop/abort/flush/wakeup。
   - `Result` 和 error mapping。

2. Core integration tests：
   - 打开小样本媒体，验证 media info。
   - 解码若干 audio/video frame。
   - seek 后 frame serial/generation 不回退。
   - stop 时 worker 和 queue 确定退出。

3. Architecture checks：
   - `sdk/media_core/include` 不包含 Qt 头。
   - `sdk/media_core/src` 不包含 QObject/QThread/QMutex/QString/QUrl/QRhi/QSG。
   - Qt adapter 可以依赖 SDK，SDK 不依赖 PlayPlugin。
   - `PlayerEngine` 不直接 include FFmpeg decoder internals。

## 接受标准

1. `media_sdk_core` 以 C++20 编译为独立 CMake target。
2. `media_sdk_core` 公开头文件无 Qt include。
3. 当前 PlayPlugin 可以通过 Qt adapter 播放现有音视频文件。
4. QML API 和用户可见行为保持稳定。
5. 现有 CTest 继续通过。
6. 新增 SDK core unit/integration/architecture tests 通过。
7. 停止、析构、seek、EOF、打开新文件路径没有线程泄漏或旧帧回放。

## 任务拆分

### 阶段 1：建立 SDK 骨架和边界检查

1. 新建 `sdk/media_core` CMake target，启用 C++20。
2. 新建 `include/media_sdk` 公共头目录。
3. 添加 architecture check，禁止 core include Qt。
4. 添加最小 `PlayerConfig`、`MediaError`、`Result`、`PlayerEvent` 类型。
5. 注册 CTest，确认 skeleton target 独立编译。

交付：空实现 SDK target + API 基础类型 + Qt-free 边界测试。

### 阶段 2：迁移纯计算和基础并发组件

1. 迁移 `VideoFrameScheduler` 为 `FrameScheduler`，用 `std::chrono`。
2. 迁移 `ClockSync`，替换 `QElapsedTimer/QMutex/QAtomicInteger`。
3. 迁移 `FrameQueue`，替换 `QMutex/QWaitCondition`。
4. 为 scheduler、clock、queue 写 C++ unit tests。

交付：播放内核基础组件可独立测试，不依赖 FFmpeg 或 Qt。

### 阶段 3：定义 frame/media/event contract

1. 定义 `MediaInfo`、`AudioFrame`、`VideoFrame`、`PlaneView`、`NativeHandle`。
2. 定义 `PlayerEvent`：media info、position、video frame、audio frame、EOF、state、error。
3. 明确 `VideoFrame` shared storage 生命周期。
4. 添加 frame metadata tests，覆盖 CPU frame 和 native handle 描述。

交付：SDK 对外数据契约稳定，Qt adapter 可以开始映射。

### 阶段 4：迁移 FFmpeg RAII、demux 和 stream decode

1. 迁移 `FFmpegUtils` 到 SDK internal。
2. 把 `MediaOpener` 改为 `Demuxer`，输入改 `std::filesystem::path`。
3. 把 `StreamFrameDecoder` 改为 core `StreamDecoder`。
4. 把 `DecodePerformance` 改为无 Qt diagnostics。
5. 添加小样本媒体 integration test，验证 open 和读取 frame。

交付：SDK core 可以打开媒体并解码原始 FFmpeg frame。

### 阶段 5：迁移 video frame processing 和硬解策略

1. 迁移 `VideoFrameProcessor`，去 Qt 时间工具。
2. 硬解 backend 名称从 `QString` 改 `std::string_view` 或 `std::string`。
3. 输出 SDK `VideoFrame` metadata。
4. 保留 VideoToolbox native frame 描述，但不暴露 Qt RHI。
5. 添加像素格式、color range、BT.601/BT.709、native handle tests。

交付：SDK core 输出可由 Qt adapter 渲染的 video frame contract。

### 阶段 6：实现 DecodeWorker 和 PlaybackController

1. 用 `std::jthread` 替换 `FFmpegDecoder : QThread` 的核心逻辑。
2. 抽 command state：open、pause、seek、stop。
3. 抽 queue policy：有音频时视频队列满可丢帧，无音频时保留 backpressure。
4. 集成 clock 和 frame scheduler。
5. 通过 `IEventSink` 输出事件。
6. 添加 seek/stop/EOF integration tests。

交付：SDK core 可独立完成播放内核状态推进。

### 阶段 7：接入 Qt adapter，保持 PlayPlugin 行为

1. 新建 `QtPlaybackAdapter`，内部持有 `media_sdk::Player`。
2. `PlayerEngine` 保持 QML API，不直接操作 SDK worker。
3. Qt adapter 把 SDK event marshal 到 GUI thread。
4. 视频 event 转成现有 `FFmpegSurface` 可消费的 frame wrapper。
5. 音频先保留现有 `QAudioSink` 路径，或通过 adapter 消费 SDK audio frame。
6. 更新 `PlaybackPipeline`，逐步移除直接持有 `FFmpegDecoder` 的职责。

交付：当前 Qt 播放器由 SDK core 驱动，用户可见行为不变。

### 阶段 8：清理旧实现和加固文档

1. 移除或降级旧 `decode/` 中已迁移的 Qt-bound 实现。
2. 更新 `PlayPluginExecutionFlow.md`。
3. 增加 architecture checks，防止 Qt 类型回流 SDK core。
4. 跑完整 build、CTest 和手动播放验证。

交付：B+ 迁移完成，项目内只保留一套播放内核实现。

## 后续演进到 C

B+ 稳定后再进入 C：

1. 定义 `IAudioSink` 和 `IVideoPresenter`。
2. 把当前 Qt RHI 路径包装成 `QtRhiPresenter`。
3. 新增 `OpenGLPresenter`。
4. 增加 presenter capability negotiation。
5. 把硬件 native handle 的 zero-copy 路径从 Qt adapter 专属逻辑提升为 presenter 能力。
6. 若 SDK 要给第三方稳定集成，设计 C ABI 或 PIMPL ABI。

这个顺序能避免同时重构解码、同步、音频、渲染和 UI。B+ 先稳定 core 边界；C 再把 presentation 变成可插拔后端。
