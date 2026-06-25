# macOS AVFoundation 原生硬解设计文档

## 背景

当前 SDK 已经把播放 core、runtime、CoreAudio 输出、Qt RHI presenter 和 VideoToolbox native texture 呈现路径拆开。`/Users/zs/Downloads/6月29日.mov` 的问题暴露出一个新的平台兼容性缺口：FFmpeg HEVC decoder 虽然声明支持 `videotoolbox_vld` hwaccel，但在该样片第一帧初始化 `hevc_videotoolbox` 时失败：

```text
DOVI ... profile: 8 ... rpu flag: 1
Main 10 profile bitstream
Format videotoolbox_vld requires hwaccel hevc_videotoolbox initialisation.
Failed setup for format videotoolbox_vld: hwaccel initialisation returned error.
Format videotoolbox_vld not usable, retrying get_format() without it.
Format yuv420p10le chosen by get_format().
```

该样片是 Apple QuickTime MOV、HEVC Main10、Dolby Vision profile 8 风格视频。FFmpeg fallback 到软件 `yuv420p10le` 后可以播放，但无法达成“Apple 生态视频稳定 native 硬解 + CVPixelBuffer 零拷贝呈现”的目标。

本设计选择 macOS 平台优先使用 AVFoundation `AVAssetReader` 作为原生视频解码 backend，输出 `CVPixelBufferRef`，再复用现有 SDK `VideoFrame` native handle、runtime A/V sync 和 Qt RHI presenter。

## 目标

1. 新增 Apple-only 平台解码模块 `media_sdk_platform_decode_macos`。
2. 使用 AVFoundation `AVAssetReader` 打开 MOV/MP4 视频轨并输出 `CVPixelBufferRef`。
3. 将 `CVPixelBufferRef` 封装为 SDK `VideoFrame`：
   - `pixelFormat = PixelFormat::Native`
   - `nativeHandle.kind = NativeHandleKind::VideoToolboxPixelBuffer`
   - `storage` 持有 `CVPixelBufferRef` 生命周期
4. 保持 `media_sdk_core` Qt-free、Objective-C-free、AVFoundation-free。
5. 保持 `media_sdk_playback_runtime` 只消费 SDK `VideoFrame`，不直接依赖 AVFoundation。
6. PlayPlugin 继续只做 Qt QML 控制和 Qt RHI 展示，不拥有平台解码策略。
7. AVFoundation native decode 失败时结构化 fallback 到现有 FFmpeg decode 路径。
8. 对 native decode 成功、fallback 原因、输出帧数、CVPixelBuffer 格式提供 diagnostics。
9. 第一版聚焦 macOS，解决 Apple MOV/HEVC Main10/Dolby Vision profile 8 兼容性。

## 非目标

1. 第一版不实现 Windows D3D11、Linux VAAPI、Vulkan、DMABUF 或 OpenGL backend。
2. 第一版不把 Qt、QML、QRhi、QSG、QObject 放入 SDK core 或平台 decode 公共接口。
3. 第一版不重写 CoreAudio 输出。
4. 第一版不删除 FFmpeg 解码路径。
5. 第一版不承诺第三方二进制 ABI。
6. 第一版不做 HDR tone mapping 或 Dolby Vision RPU 渲染增强；只保证能稳定输出可呈现的 `CVPixelBufferRef`。
7. 第一版不要求所有 HEVC 都走 AVFoundation；backend 选择必须可降级、可观测。

## 模块边界

新增模块：

```text
sdk/media_platform_decode_macos/
  include/media_sdk/platform/macos/
    AvFoundationVideoDecoder.h
    MacosNativeDecodeBackend.h
  src/
    AvFoundationVideoDecoder.mm
    MacosNativeDecodeBackend.mm
    CoreFoundationUtils.h
  tests/
    tst_avfoundation_decoder_contract.cpp
```

依赖方向：

```text
PlayPlugin
  -> media_sdk_playback_runtime
      -> media_sdk_core
      -> media_sdk_platform_decode_macos interfaces/factory injection

media_sdk_platform_decode_macos
  -> media_sdk_core frame/error types
  -> AVFoundation/CoreMedia/CoreVideo
```

禁止方向：

```text
media_sdk_core -> media_sdk_platform_decode_macos
media_sdk_core -> AVFoundation/CoreMedia/CoreVideo
media_sdk_playback_runtime -> Qt/QML/QRhi
media_sdk_platform_decode_macos -> Qt/QML/QRhi
```

## 设计原则

- **单一职责**：AVFoundation backend 只负责视频轨读取和 `CVPixelBufferRef` 输出，不管理音频设备、不管理 Qt 呈现。
- **依赖倒置**：runtime 依赖抽象 video source/backend，平台模块实现抽象并由 composition root 注入。
- **接口隔离**：平台 decode 接口只表达 open/read/seek/stop/diagnostics，不暴露 AVFoundation 对象。
- **RAII**：`CVPixelBufferRef`、`CMSampleBufferRef`、`AVAssetReader`、`AVAssetReaderTrackOutput` 必须有明确 ownership。
- **失败可恢复**：AVFoundation 打开、读取、seek、输出格式不支持时必须 fallback 到 FFmpeg，不允许播放失败。
- **可观测**：所有 backend 选择和 fallback 都必须有结构化 reason，不能只依赖日志。
- **小步提交**：按阶段提交，每阶段可独立构建或通过架构检查。

## 核心数据流

目标 native 路径：

```text
AVURLAsset
  -> AVAssetReader
  -> AVAssetReaderTrackOutput(video)
  -> CMSampleBufferRef
  -> CVPixelBufferRef
  -> media_sdk::VideoFrame
  -> RuntimePlayer video queue
  -> AvSyncScheduler
  -> QtRhiVideoPresenter
  -> FFmpegSurface
  -> AppleMetalVideoTextureBridge
  -> CVMetalTextureCacheCreateTextureFromImage
  -> QRhiTexture
  -> shader YUV->RGB
```

Fallback 路径：

```text
AVFoundation backend unavailable/failed
  -> media_sdk_core FFmpeg Demuxer/DecodeWorker
  -> software or FFmpeg VideoToolbox fallback
  -> SDK VideoFrame
  -> RuntimePlayer
```

## Backend 选择策略

第一版 backend 选择规则：

1. 仅 macOS 启用 AVFoundation native backend。
2. 仅本地文件 URL 启用；网络 URL 先走 FFmpeg。
3. 文件容器为 MOV/MP4/M4V 时优先尝试 AVFoundation。
4. 视频 codec 为 HEVC/H.264 时可尝试 AVFoundation。
5. 如果 `RuntimePlayer` 的 presenter capabilities 不支持 `VideoToolboxPixelBuffer`，不启用 AVFoundation native video backend。
6. 如果用户配置关闭 native decode 或硬件 decode，不启用 AVFoundation。
7. AVFoundation open/read 第一帧失败时，当前 open 自动 fallback 到 FFmpeg。

选择结果必须记录：

```cpp
enum class NativeDecodeBackendKind {
    None,
    AvFoundation,
    Ffmpeg
};

enum class NativeDecodeFallbackReason {
    None,
    DisabledByConfig,
    UnsupportedPlatform,
    UnsupportedContainer,
    UnsupportedCodec,
    PresenterDoesNotSupportNativeHandle,
    AssetOpenFailed,
    NoVideoTrack,
    ReaderStartFailed,
    PixelBufferUnavailable,
    ReadFailed,
    SeekFailed
};
```

## 接口草案

平台 backend 不直接替代 `media_sdk::Player` 第一版，而是作为 runtime 的可选 native video source。音频仍由 FFmpeg core 解码并由 CoreAudio 输出。

```cpp
namespace media_sdk::platform::macos {

struct NativeVideoDecoderConfig {
    bool enableNativeVideoDecode = true;
    bool requireNativeVideoDecode = false;
};

struct NativeVideoDecoderInfo {
    int width = 0;
    int height = 0;
    double fps = 0.0;
    std::chrono::milliseconds duration { 0 };
    std::string formatName;
};

struct NativeVideoDecoderDiagnostics {
    std::uint64_t framesRead = 0;
    std::uint64_t pixelBuffersPublished = 0;
    std::uint64_t readFailures = 0;
    std::uint64_t seeks = 0;
    std::uint32_t lastCvPixelFormat = 0;
    NativeDecodeFallbackReason fallbackReason = NativeDecodeFallbackReason::None;
};

class INativeVideoDecoder {
public:
    virtual ~INativeVideoDecoder() = default;

    virtual Result<NativeVideoDecoderInfo> open(const std::filesystem::path& path) = 0;
    virtual Result<std::optional<VideoFrame>> readNext() = 0;
    virtual Result<void> seek(std::chrono::microseconds position) = 0;
    virtual void stop() = 0;
    virtual NativeVideoDecoderDiagnostics diagnostics() const = 0;
};

}
```

`readNext()` 约定：

- 返回 `Result<std::optional<VideoFrame>>::success(std::nullopt)` 表示视频 EOF。
- 返回 failure 表示当前 native backend 不可继续，runtime 可触发 FFmpeg fallback。
- 返回的 `VideoFrame::storage` 必须持有 `CVPixelBufferRef`。

## 生命周期模型

### CVPixelBuffer ownership

`CVPixelBufferRef` 从 `CMSampleBufferGetImageBuffer()` 得到时是 borrowed reference。封装为 SDK `VideoFrame` 前必须 `CVPixelBufferRetain()`，并由 shared storage 析构时 `CVPixelBufferRelease()`。

```text
CMSampleBufferRef lifetime
  borrowed CVPixelBufferRef
    -> CVPixelBufferRetain
      -> VideoFrame::storage shared owner
        -> Qt presenter async present keeps VideoFrame value
          -> storage released after render completion/clear
```

Qt presenter 只能借用 `nativeHandle.handle`，不能独立释放 `CVPixelBufferRef`。

### AVAssetReader ownership

`AvFoundationVideoDecoder` 拥有：

- `AVURLAsset`
- `AVAssetReader`
- `AVAssetReaderTrackOutput`
- 当前 track metadata

`stop()` 必须 cancel reader，并保证后续 `readNext()` 返回可恢复错误或 EOF，不阻塞调用线程。

### 线程模型

第一版 AVFoundation decoder 运行在 SDK decode/native video worker 线程。它不能访问 Qt 对象。

```text
native video worker thread
  AVAssetReader copyNextSampleBuffer
  CVPixelBufferRef -> VideoFrame
  enqueue to RuntimePlayer video queue

runtime video thread
  A/V sync
  presenter present(VideoFrame)

Qt GUI/render thread
  actual QRhi/Scene Graph draw
```

`AVAssetReader` 不跨线程共享。open/read/seek/stop 对同一个 decoder 实例必须串行调用，或由 wrapper 内部加锁并保证状态转换一致。

## Seek / EOF / Generation

AVFoundation video source 必须遵守现有 runtime timeline：

- open 创建新的 session。
- seek 创建新的 generation。
- seek 时必须停止当前 reader，基于目标时间重新创建 `AVAssetReader` 和 track output。
- seek 完成前不能发布旧 generation 的 frame。
- EOF 只表示当前 generation 的视频源结束，仍需 runtime 等待音频 EOF 和 presenter drain。
- fallback 到 FFmpeg 时必须带上 resume position 和 generation 映射，避免旧帧进入新 session。

## 输出格式要求

优先请求 VideoToolbox/Metal 友好的 bi-planar YUV：

- `kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange`
- `kCVPixelFormatType_420YpCbCr8BiPlanarFullRange`
- `kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange`
- `kCVPixelFormatType_420YpCbCr10BiPlanarFullRange`

不接受 BGRA 作为第一版 native decode 输出，因为 BGRA 会让 AVFoundation 做额外颜色转换，失去 YUV native 呈现路径价值。

如果 `CVPixelBufferGetPixelFormatType()` 不在支持列表：

1. 记录 `lastCvPixelFormat`。
2. 当前 backend 返回 failure。
3. runtime fallback 到 FFmpeg。

## 与现有 Qt RHI Presenter 的关系

现有 `QtRhiVideoPresenter` 已支持 `NativeHandleKind::VideoToolboxPixelBuffer`，并可通过 `AppleMetalVideoTextureBridge` 创建 Metal native texture。AVFoundation backend 输出同一种 native handle，因此 Qt presenter 不需要知道 frame 来源。

必须保持：

```text
AVFoundation CVPixelBufferRef
FFmpeg AV_PIX_FMT_VIDEOTOOLBOX CVPixelBufferRef
```

在 SDK frame contract 中表现一致。

## Fallback 策略

fallback 必须是正式状态机，不允许临时 timer retry。

### 打开阶段 fallback

```text
try AVFoundation open
  success -> use native video source
  failure -> record reason -> open FFmpeg core path
```

### 播放中 fallback

```text
AVFoundation read/seek failure
  -> pause native source
  -> record resume position
  -> clear video queue / increment generation
  -> reopen FFmpeg core at resume position
  -> continue playback with CPU/native FFmpeg path
```

第一版可以只实现打开阶段 fallback；播放中 fallback 若未实现，必须明确返回错误并停止播放，不能静默卡死。后续阶段再补完整 live fallback。

### require native 模式

`requireNativeVideoDecode = true` 仅用于测试和诊断。该模式下 AVFoundation 失败不 fallback，而是返回结构化错误，方便确认样片是否真的走 native。

## 诊断与日志

新增 diagnostics 字段应能回答：

- 本次打开选择了哪个 video backend？
- AVFoundation 是否真的输出了 `CVPixelBufferRef`？
- 输出的 `CVPixelBuffer` pixel format 是什么？
- native frame 入 runtime 队列多少帧？
- presenter native accepted/presented 多少帧？
- fallback 原因是什么？

建议 runtime diagnostics 增加：

```cpp
std::uint64_t avFoundationVideoFrames = 0;
std::uint64_t nativeVideoDecodeFallbacks = 0;
NativeDecodeBackendKind activeVideoBackend = NativeDecodeBackendKind::None;
NativeDecodeFallbackReason nativeDecodeFallbackReason = NativeDecodeFallbackReason::None;
```

## 测试策略

### 架构检查

新增 Python 架构检查：

- `media_sdk_core` 不 include AVFoundation/CoreVideo/CoreMedia。
- `media_sdk_platform_decode_macos` 使用 Objective-C++ `.mm`。
- 平台模块链接 `AVFoundation`、`CoreMedia`、`CoreVideo`。
- `AVFoundationVideoDecoder` 出现 `AVAssetReader`、`AVAssetReaderTrackOutput`、`CVPixelBufferRetain`、`CVPixelBufferRelease`。
- PlayPlugin 不直接 include AVFoundation decoder internals。

### C++/Objective-C++ contract 测试

可做无真实视频文件的 contract 测试：

- diagnostics 默认值。
- unsupported path 返回结构化错误。
- `CVPixelBufferRef` storage retain/release helper 行为。

真实样片测试因依赖 `/Users/zs/Downloads/6月29日.mov`，不进入默认 CTest，但提供本地手动验证命令。

### 手动验证

使用样片：

```text
/Users/zs/Downloads/6月29日.mov
```

验收标准：

1. require native 模式下，AVFoundation 能 open 并读出第一帧。
2. 第一帧 `VideoFrame.pixelFormat() == PixelFormat::Native`。
3. `nativeHandle.kind == NativeHandleKind::VideoToolboxPixelBuffer`。
4. `CVPixelBufferGetPixelFormatType()` 是支持的 420 bi-planar 格式。
5. 正常播放时 `avFoundationVideoFrames` 和 `nativePresented` 增长。
6. FFmpeg `hevc_videotoolbox` 失败不再影响该样片 native 播放路径。

## 风险

1. AVFoundation 可能输出 BGRA 或系统选择的 pixel format，需要强约束 output settings 并验证。
2. AVAssetReader 只负责本地文件离线读取，不适合第一版处理复杂网络流。
3. AVFoundation video 和 FFmpeg audio 分属两个 demux/source，seek generation 和 PTS 对齐必须严谨。
4. Dolby Vision profile 8 的显示效果可能仍按 base layer/HDR 或 SDR 兼容层呈现，第一版不解决 tone mapping。
5. Runtime 当前围绕 core `PlayerEvent` 设计，接入第二 video source 需要小心避免双 video source 同时发布帧。

## 阶段任务

### 阶段 1：设计与架构约束

- 新增本文档。
- 新增/更新架构检查，锁定平台模块边界。
- 提交文档。

### 阶段 2：空平台模块

- 新增 `sdk/media_platform_decode_macos/CMakeLists.txt`。
- 顶层 CMake 在 Apple 下加入该模块。
- 建立空 target、include 目录和 contract test。
- 不接入播放链路。

### 阶段 3：Native VideoFrame Storage

- 增加 CVPixelBuffer storage RAII helper。
- 验证 retain/release。
- 构造 `VideoFrame` native handle contract。

### 阶段 4：AVFoundation Reader 最小实现

- `open(path)` 读取 asset、选择 video track、创建 reader。
- `readNext()` 输出第一帧 `CVPixelBufferRef`。
- require native 测试模式下可对样片做本地验证。

### 阶段 5：时间戳与媒体信息

- 从 `CMSampleBuffer` 提取 PTS。
- 从 track 提取 width/height/fps/duration。
- 输出 `NativeVideoDecoderInfo`。

### 阶段 6：Backend 选择策略

- 根据平台、container、codec、presenter capability、config 选择 AVFoundation 或 FFmpeg。
- 打开阶段 fallback 到 FFmpeg。
- diagnostics 记录 active backend 和 fallback reason。

### 阶段 7：Runtime 视频源接入

- runtime 支持可选 native video source。
- 确保只有一个 video source 发布当前 generation 的 video frames。
- 音频仍走 FFmpeg/CoreAudio。

### 阶段 8：Seek / Stop / EOF

- seek 重建 AVAssetReader。
- stop cancel reader。
- EOF 与 runtime drain 对齐。
- 旧 generation 帧丢弃。

### 阶段 9：PlayPlugin Composition Root 接入

- PlayPlugin 创建 macOS native decode backend 并注入 runtime。
- Qt presenter capability 决定是否启用。
- 保持 QML API 不变。

### 阶段 10：样片验证与性能回归

- 用 `/Users/zs/Downloads/6月29日.mov` 验证 AVFoundation native 帧。
- 验证 `nativePresented` 增长。
- 验证暂停、seek、stop、EOF。
- 全量构建和 CTest。

## 验收标准

最终实现完成时必须满足：

1. 样片可通过 AVFoundation 输出 native `CVPixelBufferRef`。
2. 播放路径不依赖 FFmpeg `hevc_videotoolbox` 成功。
3. FFmpeg fallback 仍可播放普通视频。
4. `media_sdk_core` 不依赖 Apple framework。
5. PlayPlugin 不直接管理 AV 队列、主时钟或平台解码策略。
6. 所有新增平台对象生命周期有 RAII 或明确释放路径。
7. 相关 CTest 通过，全量 build 通过。
