# Media SDK Seek Resolution 与 Clock Anchor 设计文档

## 背景

当前 Media SDK 已经实现了精确 seek 的基础能力：demuxer 回退到可解码点，decoder flush 后进入 seek gate，目标点前的视频帧会丢弃，跨越目标点的音频帧会裁剪。最近一次修复又把 `SeekCompletedEvent` 提前到目标帧进入交付路径前，解决了 session/runtime generation 映射死锁。

手动测试暴露了新的专业性问题：用户点击 seek 后，进度条先显示目标点，随后被第一帧音频 PTS 拉到更靠后的位置。

日志证据：

```text
PlayerEngine: seek to 4763ms
runtime seek target=4763ms session=1 generation=2
runtime afterFlush ok=1 clock=0ms clockGen=2 valid=1
ui target=4763ms generation=1 immediatePosition=4763ms
session seekCompleted ... payload=4763ms targetGate=4763ms runtime=1/2
session position corePayload=4763ms runtimeClock=0ms gate=4763ms
session position dropped belowGate runtimeClock=0ms gate=4763ms
runtime firstAudioAfterSeek pts=6035ms generation=2 clockAfterWrite=6035ms
adapter position forwarded=6035ms
ui apply decoderPosition=6035ms previous=4763ms
```

直接原因是 runtime seek 后没有建立 seek target clock anchor。`RuntimePlayer::seek(position)` 当前只 flush audio/video queues 和 audio output，并没有使用 `position` 设置播放时钟基线。CoreAudio 在 flush 后 clock 回到 `0ms`，首个新音频 buffer 写入时又把 clock 基线设为该 buffer 的 PTS。因此首个音频 PTS 为 `6035ms` 时，UI 会从 `4763ms` 被拉到 `6035ms`。

这个问题不应在 QML 层压制。进度条显示、视频调度和音频播放必须共享同一个 SDK 内部 seek/clock 合同。

## 目标

1. seek 后对外 position 不再从 requested target 突然跳到首个音频 PTS。
2. runtime 真正使用 `RuntimePlayer::seek(position)` 的 `position` 参数建立 clock anchor。
3. 当 `firstAudioPts` 晚于 requested target 时，SDK 使用明确的 audio gap policy 处理，不让 audio clock 直接跳变。
4. 保持 `SeekCompletedEvent` 可兼容当前调用方，同时逐步补充 seek resolution 信息。
5. 不回退 generation 死锁修复，不恢复 seek 时清屏，不在 QML 中做临时 clamp。
6. 对 playing seek、paused seek、audio-only、video-only、audio+video、fallback seek 建立可重复测试。

## 非目标

1. 不在本阶段实现字幕 seek 或帧号级 seek。
2. 不把 UI 拖动状态、QML slider debounce 作为主要修复手段。
3. 不把 decoder 预读 position 重新作为外部播放进度；decoder position 仍然可能大幅领先实际播放。
4. 不引入无界静音填充；异常大 gap 必须有上限和明确降级。
5. 不重写 FFmpeg demuxer，也不拆分当前单 worker 解码架构。

## 核心设计

推荐方案为 **Seek Resolution + Clock Anchor + Audio Gap Fill**。

### Seek Resolution

Core 层负责识别 seek 请求和目标侧媒体边界。长期事件合同应表达：

```cpp
struct SeekResolution {
    std::chrono::milliseconds requestedPosition { 0 };
    std::chrono::milliseconds resolvedPosition { 0 };
    std::optional<std::chrono::milliseconds> firstAudioPts;
    std::optional<std::chrono::milliseconds> firstVideoPts;
    bool exact = true;
    bool audioGap = false;
};
```

语义：

- `requestedPosition`：用户请求的目标，例如 `4763ms`。
- `resolvedPosition`：SDK 对外承认的 seek 落点。推荐默认等于 requested target。
- `firstAudioPts`：精确预滚后第一段会进入 runtime 的音频 PTS。
- `firstVideoPts`：精确预滚后第一帧会进入 runtime 的视频 PTS。
- `audioGap`：`firstAudioPts` 明显晚于 `resolvedPosition`，runtime 需要补 gap 或使用非音频 clock。
- `exact`：没有进入缺失 PTS、discard limit、near EOF 等降级路径。

第一阶段不强制一次性改完 public event。可先保持：

```cpp
struct SeekCompletedEvent {
    std::chrono::milliseconds position { 0 };
};
```

其中 `position` 继续表示对外 resolved position。后续可在末尾追加默认字段，保持大部分 designated initializer 兼容：

```cpp
struct SeekCompletedEvent {
    std::chrono::milliseconds position { 0 };
    std::chrono::milliseconds requestedPosition { 0 };
    std::optional<std::chrono::milliseconds> firstAudioPts;
    std::optional<std::chrono::milliseconds> firstVideoPts;
    bool exact = true;
    bool audioGap = false;
};
```

### Clock Anchor

Runtime 层新增 seek clock anchor。`RuntimePlayer::seek(position)` 必须保存：

```cpp
struct SeekClockAnchor {
    bool active = false;
    runtime::Generation generation = 0;
    std::chrono::microseconds requestedPosition { 0 };
    std::optional<std::chrono::microseconds> firstAudioPts;
};
```

规则：

1. seek 开始时，anchor 绑定 runtime generation 和 requested position。
2. audio output flush 后的 `0ms` clock 不允许作为 seek 后外部 position。
3. 首个音频 frame 到达前，external clock 至少稳定在 requested position。
4. 首个音频 frame 到达后：
   - 如果 `firstAudioPts <= requestedPosition + tolerance`，正常切回 audio clock。
   - 如果 `firstAudioPts > requestedPosition + tolerance`，进入 audio gap fill。
5. seek 被 stop/open/新 seek/fallback 替换时，anchor 必须随 generation 失效。

### Audio Gap Fill

当 seek target 到第一段真实音频之间有 gap 时，专业播放器不应让 audio clock 直接跳到真实音频 PTS。推荐先实现 silent gap fill：

```text
requestedPosition = 4763ms
firstAudioPts = 6035ms
gap = 1272ms

write silence pts=4763ms duration=1272ms generation=2
write real audio pts=6035ms generation=2
```

这样 CoreAudio ring buffer 会把 playback position 初始化为 `4763ms`，播放静音期间 clock 平滑推进，到 `6035ms` 后自然进入真实音频。视频 scheduler 继续以同一个 audio clock 调度，不会把 `4763ms~6035ms` 的视频判 late 丢掉。

约束：

- Gap 小于等于 `audioGapTolerance` 时不填充，避免 sample rounding 导致过度处理。
- Gap 大于 `maxSeekAudioGapFill` 时不一次性填充无限静音。第一阶段建议上限 `2s`，超过上限时：
  - 标记 best-effort。
  - `resolvedPosition` 可调整到 `firstAudioPts`，或者使用 video/self clock。具体策略在实现任务中用测试锁定。
- 静音必须按 runtime audio format 生成：
  - `UInt8`：中心值 `0x80`。
  - `Int16` / `Int32` / `Float32`：全 0。
  - `Float32Planar`：当前 runtime audio path 不应输出 planar；如遇到，返回明确错误或不填充。
- 静音写入要分块，不能为大 gap 分配过大 buffer。

### Position Event 合同

外部 `PositionChangedEvent` 继续代表“正在播放或展示的媒体时间”，不代表 decoder 预读进度。

Session 层仍然负责：

- generation/timeline stale filtering。
- seek pending 期间丢弃小于 target gate 的 position。
- 不转发 duplicate position。

Runtime 层负责提供可信 clock：

- 对外 clock 和 video scheduler 使用同一套 effective clock。
- seek anchor/gap fill 生效时，不直接暴露 audio output flush 后的 `0ms`。
- gap fill 后，audio output clock 可以自然成为 authoritative clock。

### Completion 时机与死锁约束

不能回到“frame push accepted 后才发 completion”的模型，否则会重现 session/runtime generation 死锁。Core 仍需在目标帧进入交付路径前发出 completion，让 session 先接受新 generation。

但 completion 的 `position` 必须表示 resolved position，而不是随第一帧 audio PTS 改写。Runtime 通过 anchor/gap fill 保证后续 clock 与 resolved position 连续。

## 分阶段落地

### Phase 1：修复 clock jump

目标：不改变 public event，仅修复 `4763ms -> 6035ms` 这种 UI/clock 跳变。

工作：

1. 移除临时 `SeekTrace` 或改为受控诊断。
2. 添加 runtime 单元测试：seek target 后，首个 audio PTS 晚于 target，不应让 external clock 直接跳到首音频 PTS。
3. 新增 runtime silent gap helper。
4. `RuntimePlayer::seek(position)` 保存 anchor。
5. `audioLoop()` 在首个 seek 后音频 frame 写入前补静音 gap。
6. video scheduling 使用补 gap 后的 audio clock，自然避免 early video 被判 late。

### Phase 2：补齐 seek resolution

目标：Core 和 Session 能解释 seek 实际落点，便于诊断和后续策略。

工作：

1. 在 core seek gate 中记录 first target-side audio/video PTS。
2. 扩展 `SeekCompletedEvent` 默认字段，保持 `position` 兼容。
3. Session/Adapter 继续以 `position` 做 pending seek mapping，同时可记录 diagnostics。
4. 增加测试覆盖 `requestedPosition`、`firstAudioPts`、`firstVideoPts`。

### Phase 3：处理异常 gap 与无音频策略

目标：覆盖大 gap、near EOF、video-only、audio-only。

工作：

1. 引入 `audioGapTolerance` 和 `maxSeekAudioGapFill` 配置，先放 runtime internal config。
2. 超过上限时明确选择 best-effort 策略。
3. video-only 使用 master/video clock anchor，不依赖 audio output。
4. audio-only 继续以 audio clock 为主，必要时填静音。

## 测试策略

必须新增或更新以下测试：

1. `media_sdk_playback_runtime_player_mock`
   - seek target `4763ms`，首个 audio PTS `6035ms`，验证写入静音 gap 后 clock 从 target 起步。
   - 小 gap 不填充。
   - 大 gap 不无限分配。
   - 新 seek 取消旧 anchor。

2. `media_sdk_platform_audio_macos` tests
   - CoreAudio ring buffer 写入 silence 后 clock 基线正确。
   - 不同 sample format 的 silence bytes 正确。

3. `media_sdk_playback_session_event_router`
   - runtime clock 从 target 平滑推进时，session 转发 target-side position。
   - runtime clock 小于 target gate 时仍丢弃。

4. `media_sdk_core_playback_worker`
   - first audio/video PTS 被记录到 seek resolution。
   - 现有 accurate seek 行为不回退。

5. PlayPlugin adapter tests
   - pending seek request 仍按 completion `position` 映射。
   - UI 不依赖新增 resolution 字段。

## 验收标准

1. 对日志案例，seek 到 `4763ms` 后，首个 UI position 不应跳到 `6035ms`。
2. 如果 runtime 需要处理 `4763ms -> 6035ms` gap，应写入静音或使用受控 best-effort 策略，不能直接暴露跳变。
3. 视频不应因为 audio output clock 直接跳到 `6035ms` 而丢弃 `4763ms~6035ms` 之间的目标侧视频帧。
4. seek 后播放、暂停、fallback、EOF 行为保持现有测试通过。
5. 全量 `ctest` 通过。

## 风险与控制

1. **静音填充过大导致内存或延迟问题**  
   使用分块写入和 `maxSeekAudioGapFill` 控制。

2. **错误填充掩盖 demux seek 漏读**  
   Phase 2 通过 `firstAudioPts/firstVideoPts` 记录和 diagnostics 区分真实媒体 gap 与 seek 锚点问题。

3. **paused seek 行为改变**  
   paused seek 可预填音频队列，但不能 resume audio device；测试必须覆盖 pause 状态不变。

4. **fallback seek 与普通 seek anchor 冲突**  
   anchor 绑定 runtime generation；fallback 新 generation 自动失效旧 anchor。

5. **public API 兼容性**  
   第一阶段不改公开事件字段；第二阶段只追加默认字段，不删除 `position`。

