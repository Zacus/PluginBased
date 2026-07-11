# Media SDK Accurate Seek 设计文档

## 背景

当前 `media_sdk_core::DecodeWorker::handleSeek()` 使用：

```cpp
av_seek_frame(m_media.formatContext.get(), -1, targetUs, AVSEEK_FLAG_BACKWARD);
```

这会把 demuxer 定位到目标时间之前的关键点。随后代码 flush decoder、递增 generation，并立即发出 `SeekCompletedEvent` 和 `PositionChangedEvent`。播放中 seek 会直接进入 `decodeUntilBlocked()`，暂停 seek 会进入 `decodeSeekPreroll()`，两条路径都会把 seek 后解码出的第一批帧直接推给 `IDecodeFrameSink`。

这个行为是“关键帧回退 seek”，不是专业播放器常见的“精确 seek”。对于 GOP 较长的视频或音频 seek 落点不在 sample frame 边界的媒体，用户可能会在 seek 到 10s 后先看到或听到 9.xs 的内容。Session 层当前只抑制小于目标点的 position event，不会阻止过早的 audio/video frame 进入 runtime。

## 目标

1. 默认 seek 语义升级为 `Accurate`：先回退到可解码点，再解码并丢弃目标 PTS 前的帧。
2. 视频 seek 后送出的第一帧必须满足 `frame.pts >= target`，除非文件缺失可靠 PTS 且触发明确 fallback。
3. 音频 seek 后送出的第一段音频不得早于目标点；跨越目标点的 audio frame 需要按 sample frame 裁剪。
4. `SeekCompletedEvent` 表示 seek gate 已经到达可播放目标位置，而不是仅表示 FFmpeg 粗定位成功。
5. 精确 seek 逻辑留在 `media_sdk_core`，不污染 session/runtime 队列、音频时钟和 presenter 背压。
6. 保持当前 public API 可用，第一阶段不要求 PlayPlugin 改调用方式。
7. 用可重复测试覆盖 playing seek、paused seek、audio-only、video-only、audio+video 和 near-EOF 场景。

## 非目标

1. 不在第一阶段拆分 demux/audio decode/video decode 多线程。
2. 不把精确 seek filtering 放到 `media_sdk_playback_session` 或 `media_sdk_playback_runtime`。
3. 不在第一阶段承诺帧号级别 seek 或 subtitle seek。
4. 不引入无界 preroll 或无限等待；坏文件和无 PTS 文件必须有明确降级。
5. 不重写 FFmpeg demux/decoder wrapper。
6. 不改变 PlayPlugin 的 Qt presenter、QML 控制流或 runtime presenter 背压策略。

## 专业 SDK 语义

长期接口建议支持三种 seek mode：

```cpp
enum class SeekMode {
    Fast,
    Accurate,
    Exact
};
```

第一阶段采用内部默认 `Accurate`，保留 `Player::seek(std::chrono::milliseconds)` 现有 API。

- `Fast`：仅执行 `AVSEEK_FLAG_BACKWARD` 粗定位，适合拖动预览或低延迟场景。第一阶段不暴露。
- `Accurate`：回退关键点后解码预滚，丢弃目标前 video frame，裁剪或丢弃目标前 audio samples。第一阶段默认实现。
- `Exact`：预留给未来 frame index、sample exact、或容器/codec 特化实现。第一阶段不实现。

`SeekCompletedEvent` 的语义改为：

```text
seek command accepted
  -> demuxer seeks to a decodable point at or before target
  -> decoder flushes and starts preroll
  -> seek gate receives the first playable target frame/audio
  -> emit SeekCompletedEvent(target)
```

如果目标接近 EOF，允许在 EOF reached 且没有更多可播放帧时完成 seek 并进入 finished/drained 路径，但不能无限 preroll。

## 架构选择

### 推荐方案：DecodeWorker 内部 seek gate

在 `media_sdk_core::DecodeWorker` 内部增加 `PendingSeek` 状态和小型 helper，用于在 decode frame callback 中决定 frame 是丢弃、裁剪、推送，还是触发 seek completion。

数据流：

```text
Player::seek(target)
  -> DecodeWorker::submitSeek(target)
  -> DecodeWorker::handleSeek(target)
      av_seek_frame(..., AVSEEK_FLAG_BACKWARD)
      avcodec_flush_buffers(video/audio)
      ++generation
      pendingSeek = { target, generation, mode = Accurate }
  -> decodeUntilBlocked / decodeSeekPreroll
      decode packet
      normalize frame pts
      seek gate filters video/audio before m_frames.push*
      first target media accepted -> emit SeekCompletedEvent(target)
```

这个方案的边界最清晰：

- Core 已经拥有 decoder PTS、audio sample format、sample rate、channels 和 frame push 回调。
- Session/runtime 不需要知道 preroll 细节。
- 过早 frame 不会进入 runtime queue，不会污染 audio clock、video scheduler、EOF drain 或 backpressure diagnostics。

### 不推荐方案：Session 层过滤

Session 层可以看到 core metadata 和 runtime timeline，但它只拿到 SDK `AudioFrame` / `VideoFrame` 后再转发。此时音频裁剪需要复制或重建 frame，且 backpressure 已经跨层传播，职责不清。

### 不推荐方案：Runtime 层过滤

Runtime 层过滤太晚。audio frame 进入 runtime 后会影响 queue capacity、write timing、clock generation 和 EOF drain。视频也可能已经参与 presenter backpressure。精确 seek 不应成为 runtime 的同步策略。

## 新增内部模型

建议在 `sdk/media_core/src/` 新增两个小型 helper，避免继续膨胀 `DecodeWorker.cpp`：

```text
SeekPrerollGate.h
  Tracks target PTS and per-stream readiness.
  Decides whether decoded video/audio frames are before target.

SeekAudioTrimmer.h
  Trims interleaved audio frame bytes to target sample frame.
  Rewrites AudioFrame PTS to the first retained sample time.
```

`DecodeWorker` 内部状态：

```cpp
enum class SeekMode {
    Fast,
    Accurate,
    Exact
};

struct PendingSeek {
    std::chrono::microseconds target { 0 };
    std::uint64_t generation = 0;
    SeekMode mode = SeekMode::Accurate;
    bool videoReady = false;
    bool audioReady = false;
    bool completionSent = false;
};
```

Readiness rules：

- video-only：第一帧 `pts >= target` 后 ready。
- audio-only：第一段 audio samples 覆盖或晚于 target 后 ready。
- audio+video：video ready 作为 visual completion 条件；audio 仍独立裁剪，不能送 target 前 samples。
- no reliable PTS：bounded fallback，不把无 PTS frame 当作精确 completion 的依据。

## 视频 discard 策略

视频使用 `StreamDecoder::normalizeFramePts()` 后的 display PTS。

规则：

1. `frame->pts == AV_NOPTS_VALUE` 且 pending accurate seek 未完成：丢弃，直到出现有效 PTS 或达到 fallback 上限。
2. `framePts < target`：丢弃，不调用 `VideoFrameProcessor::process()`，避免 native handle/cpu transfer 额外成本。
3. `framePts >= target`：处理并 push；如果 push accepted/backpressured，则 video ready。
4. `DecodeFramePushStatus::StaleGeneration`：继续处理为 stale，不触发 seek completed。
5. `DecodeFramePushStatus::Cancelled` 或 `Closed`：按现有语义停止或 reject，不伪造 seek completed。

## 音频 trim 策略

音频必须按 sample frame 裁剪，而不是只整帧丢弃。

输入为当前 SDK 已发布的 interleaved audio frame：

- `Float32Interleaved`
- `Signed16Interleaved`
- `Signed32Interleaved`

计算：

```text
frameStart = audioFrame.pts()
bytesPerSample = sampleFormatBytes(format)
bytesPerAudioFrame = bytesPerSample * channels
sampleDurationUs = 1'000'000 / sampleRate
trimFrames = ceil((target - frameStart) * sampleRate / 1'000'000)
trimBytes = trimFrames * bytesPerAudioFrame
newPts = frameStart + trimFrames * 1'000'000us / sampleRate
```

规则：

1. audio frame 完全早于 target：丢弃。
2. audio frame 覆盖 target：裁剪前半段，保留 target 之后 bytes，重写 PTS。
3. audio frame 晚于 target：直接 push。
4. unsupported sample format、invalid sample rate、invalid channels：返回明确失败，走 `DecodeFailed`，不要静默送错音频。

裁剪必须保持完整 interleaved sample frame，不产生半个 sample 或半个 channel。

## SeekCompletedEvent 时机

当前 `handleSeek()` 立即发送 completion，需要改为由 seek gate 发送。

建议 helper API：

```cpp
void maybeEmitSeekCompletedAfterAcceptedFrame();
```

触发条件：

- `pendingSeek.has_value()`
- `pendingSeek.generation == m_generation`
- 当前 stream readiness 达到该媒体类型的 completion 条件
- 至少一个 target-side frame/audio 已经 accepted 或 backpressured

事件顺序：

```text
SeekCompletedEvent(target)
PositionChangedEvent(target)
first target frame/audio push continues
```

这样 PlayPlugin pending seek 映射仍能用目标 position 匹配 completion，UI 不会看到 target 前 position 回跳。

## Playing Seek 与 Paused Seek

Playing seek：

1. `handleSeek()` 设置 pending seek。
2. `decodeUntilBlocked()` 继续 decode。
3. target 前 frames 被丢弃/裁剪，不进入 runtime。
4. completion 发出后保持 playing。

Paused seek：

1. `handleSeek()` 设置 pending seek。
2. `decodeSeekPreroll()` 解码到第一帧 target-side video 或 audio。
3. target 前 frames 被丢弃。
4. completion 发出后停止 preroll，不切到 playing。

暂停 seek 的关键要求是：预渲染画面必须是 target-side frame，不能显示回退关键帧。

## EOF 与 bounded preroll

精确 seek 不能无限解码。建议限制：

- 最大 discard video frames：300。
- 最大 discard audio frames：1000。
- 最大 preroll media duration：5s。

任一上限触发时：

- 如果已经见到 target-side audio 或 video：按当前可用 stream 完成。
- 如果完全没有 target-side frame：发 `SeekCompletedEvent(target)` 后交给 EOF/Finished 路径，或发 `DecodeFailed`，由测试固定最终语义。

第一阶段建议采用“EOF 附近完成 seek 并允许 finished”，避免坏文件导致 worker 卡死。

## 错误处理

- `av_seek_frame` 失败：保持现有 `SeekFailed`。
- decoder 返回错误：保持现有 `DecodeFailed`。
- audio trim 遇到不支持格式或不完整 frame：返回 `DecodeFailed`。
- PTS 长期缺失：bounded fallback，不把 worker 卡死。
- stale generation：不发送 seek completed，不修改 pending seek。

## 线程与生命周期

`PendingSeek` 只属于 `DecodeWorker` worker thread，不需要额外锁。控制命令仍通过现有 command queue 串行进入 worker。`IDecodeFrameSink` 回调仍可能阻塞，因此 seek gate 必须在 push 之前做 discard/trim，减少不必要的跨层阻塞。

`AudioFrame` 和 `VideoFrame` 继续使用值语义和 owned sample/vector storage。裁剪音频时创建新的 owned `AudioFrame`，不暴露指向临时 buffer 的 span。

## 测试策略

新增测试分三层：

1. helper 单元测试：
   - video frame before target is discarded。
   - video frame at/after target is accepted。
   - audio frame before target is discarded。
   - audio frame crossing target is trimmed with corrected PTS。
   - invalid audio metadata fails explicitly。

2. `DecodeWorker` contract tests：
   - playing seek 不推 target 前 frame。
   - paused seek preroll 不推 target 前 video。
   - burst seek 只完成最后一个 target。
   - seek near EOF 不挂死。

3. session/runtime regression tests：
   - `SeekCompletedEvent` 延后后，session timeline、pending seek mapping 和 PlayPlugin `SdkPlaybackAdapter` 仍能匹配 generation。
   - audio clock 不被 target 前音频污染。

## 分阶段落地

### Phase 1：内部 Accurate 默认

- 不改 public API。
- `Player::seek(position)` 使用 accurate gate。
- 增加 helper 和 core tests。
- 保留未来 `SeekMode` 的内部枚举。

### Phase 2：公开 SeekOptions

后续如果需要拖动预览性能，可以新增：

```cpp
struct SeekOptions {
    std::chrono::milliseconds position { 0 };
    SeekMode mode = SeekMode::Accurate;
};
```

并保留现有 `seek(position)` 作为 `Accurate` shorthand。

### Phase 3：Exact 特化

对支持索引或帧号定位的容器/codec 做精确增强。此阶段不纳入当前计划。

## 风险与缓解

1. **Seek latency 增加**：bounded preroll 限制最大丢弃量，后续通过 `SeekMode::Fast` 提供低延迟选择。
2. **SeekCompletedEvent 时机变化**：通过 session/adapter tests 固定新语义，避免 UI pending seek 误判。
3. **Audio trim 引入 sample 对齐 bug**：使用 interleaved sample frame 单元测试覆盖不同 sample format、channels、target offset。
4. **无 PTS 媒体行为不稳定**：明确 fallback 上限，不把无 PTS frame 当作精确 completion。
5. **Runtime clock 污染**：filter 必须在 core push 前完成，禁止 runtime 再丢 target 前 audio。

## 成功标准

1. seek 后进入 runtime 的第一帧视频 PTS 不早于目标点。
2. seek 后进入 runtime 的第一段音频 PTS 不早于目标点，跨目标音频已裁剪。
3. paused seek 预渲染画面不显示 target 前关键帧。
4. `SeekCompletedEvent` 只在目标侧媒体准备好后发送。
5. `ctest --test-dir build -R "media_sdk_core|media_sdk_playback_session|media_sdk_playback_runtime" --output-on-failure` 通过。
6. `cmake --build build --parallel` 通过。
