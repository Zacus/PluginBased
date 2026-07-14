# 真正的倍速播放任务拆分

## 1. 执行规则

- 依据：`docs/design/playback-rate.md`
- 每个任务独立实现、验证并提交一次。
- 每次提交只包含本任务相关文件，不混入当前工作区其他未提交改动。
- 前一任务的测试通过后再开始后一任务。
- 如实现中需要改变设计契约，先更新设计文档并说明原因。

## 2. 任务总览

| ID | 任务 | 依赖 | 状态 |
| --- | --- | --- | --- |
| R1 | 建立播放速率与媒体时钟契约 | 无 | 已完成 |
| R2 | 实现 FFmpeg 音频保调变速模块 | R1 | 已完成 |
| R3 | 接入 runtime 音频与视频倍速链路 | R1、R2 | 已完成 |
| R4 | 实现 session 动态切速时间线 | R3 | 已完成 |
| R5 | 贯通 PlayPlugin 控制与 QML UI | R4 | 已完成 |
| R6 | 完成真实媒体 benchmark 与数据验收 | R5 | 待开始 |

## 3. R1：建立播放速率与媒体时钟契约

### 目标

先固定速率校验、媒体时间换算和 video-only 时钟语义，不引入 FFmpeg 处理器。

### 改动范围

- 增加共享 playback rate 校验与默认值。
- `RuntimePlayerConfig` 增加速率配置。
- `AudioBufferView` 携带当前 generation 的速率。
- 明确 `ClockSnapshot` 中媒体时间与设备时长字段语义。
- CoreAudio ring 以 anchor、累计 sample cursor 和速率计算媒体位置。
- `MasterClock` 支持按速率外推，并在速率/代际变化时重置平滑状态。
- `AvSyncScheduler` 支持速率换算后的墙钟等待时间。
- 为 video-only runtime 权威时钟预留可读取契约。

### 测试

- 速率合法值、边界值、NaN、Infinity 和越界测试。
- CoreAudio ring 在 `0.5x/1.0x/2.0x` 下的位置换算测试。
- 同 generation 混入不同速率时的错误测试。
- MasterClock 外推、暂停、恢复、重置和长时间舍入误差测试。
- AvSyncScheduler 不同速率下 wait/present/drop 决策测试。
- 架构回归检查保持 runtime 不含 Qt、CoreAudio 和 FFmpeg 具体依赖。

### 完成标准

- 所有时钟均报告媒体时间。
- `1.0x` 行为与当前基线一致。
- runtime 单元测试和全量构建通过。

### 建议提交

```text
[功能新增] 建立倍速播放时钟契约
```

## 4. R2：实现 FFmpeg 音频保调变速模块

### 目标

提供 runtime-neutral 接口和基于 `libavfilter/atempo` 的独立具体实现。

### 改动范围

- 在 runtime 公共接口中增加 `IAudioTempoProcessor`。
- 定义 configure/process/drain/reset 及拥有型输出 buffer 契约。
- 新增 `sdk/media_audio_ffmpeg` CMake target。
- vcpkg FFmpeg feature 增加 `avfilter`。
- CMake 通过 pkg-config 导入并链接 `libavfilter`。
- 实现 `abuffer -> atempo -> abuffersink` 处理链。
- 处理首帧 PTS 锚点、多块输出、无输出、flush 和 EOS drain。
- `1.0x` 不创建或运行滤镜图。

### 测试

- 生成确定性 Float32 正弦输入，验证各预设速率的输出 sample 数。
- 频谱或过零率验证主频基本不变。
- 分块输入与一次性输入产生一致的时长和连续 PTS。
- 短输入 drain 不丢失尾部。
- reset 后不泄漏上一 generation 的数据。
- 重复 configure/reset/drain 和错误输入测试。

### 完成标准

- 处理器不依赖 Qt 和平台音频 API。
- 输入/输出 PTS 单调，duration 与速率关系正确。
- ASan 下无 AVFrame、AVFilterGraph 或 buffer 生命周期问题。

### 建议提交

```text
[功能新增] 增加FFmpeg音频保调变速处理
```

## 5. R3：接入 runtime 音频与视频倍速链路

### 目标

让固定速率配置从播放开始到 EOF 全链路生效，暂不开放运行中切速。

### 改动范围

- `RuntimePlayerDependencies` 注入 `IAudioTempoProcessor*`。
- audio thread 在非 `1.0x` 时处理 PCM，再写入 `IAudioOutput`。
- tempo 输出携带媒体 PTS、generation 和 playback rate。
- seek gap silence 按 `mediaGap / rate` 生成 sample。
- 软件 seek 兜底时钟按 `elapsed * rate` 推进。
- 音频 EOS 先 drain tempo，再等待平台输出消费完成。
- video thread 为 AvSyncScheduler 和 video-only MasterClock 设置速率。
- 增加 tempo、时钟和 late-drop 诊断计数。
- 明确 stop/seek/fallback/shutdown 的 reset 顺序。

### 测试

- runtime 假 tempo processor 的零输出、多输出、失败和 drain 测试。
- audio-only、A/V、video-only 在各预设速率下的时钟推进测试。
- seek gap silence sample 数与媒体时钟测试。
- EOS 尾部消费完成前不得发布完成事件。
- stop/seek 后旧 tempo 输出不得进入新 generation。
- 无处理器时 `1.0x` 成功，非 `1.0x` 有声媒体明确失败。

### 完成标准

- 从 open 起设置固定非 `1.0x` 时，三类媒体均按目标速率播放。
- 音频滤镜不在平台实时回调中执行。
- runtime 测试、架构检查和全量构建通过。

### 建议提交

```text
[功能修改] 接入倍速音频与调度时钟
```

## 6. R4：实现 session 动态切速时间线

### 目标

支持播放中和暂停中切速，保证旧速率缓存与新时间线严格隔离。

### 改动范围

- `PlaybackSessionDependencies` 透传 tempo processor。
- 增加 `PlaybackSession::setPlaybackRate()` 和 `playbackRate()`。
- 将 seek 与 rate change 抽象为统一的 discontinuity 控制流程。
- session 从 runtime 权威时钟捕获当前位置，不依赖 UI 进度。
- runtime 切速时递增 generation，清队列、flush 输出并 reset processor/scheduler。
- event router 记录 `Seek` 或 `RateChange` 原因及请求序列号。
- 内部精确 seek 完成后发布 `PlaybackRateChangedEvent`，不泄漏内部 seek 事件。
- 连续切速和 seek 交错采用 latest-wins，过期完成事件不得提交。
- 暂停中切速保持暂停；Completed 状态只保存速率，不隐式 replay。
- native fallback 与重新 open 保留当前速率。

### 测试

- Playing、Paused、Seeking、Completed 状态切速测试。
- `1.0 -> 2.0 -> 0.5` 连续快速切换，只接受最后请求。
- seek 与 rate change 交错时的 generation/event mapping 测试。
- 切速内部 seek 失败时不发布成功事件，并进入确定的错误状态。
- video-only 切速位置来自 runtime 权威时钟。
- 关闭过程中存在待完成切速时无死锁、悬空回调或旧事件。

### 完成标准

- 动态切速不播放旧 generation 的 PCM 或视频帧。
- 成功和失败事件语义可由 SDK 使用者可靠观察。
- session/runtime 测试和全量构建通过。

### 建议提交

```text
[功能修改] 贯通倍速播放时间线切换
```

## 7. R5：贯通 PlayPlugin 控制与 QML UI

### 目标

让用户可以从播放控制栏选择倍速，并补齐 real/fake adapter 行为。

### 改动范围

- `PlaybackPipeline` 拥有 `FfmpegAudioTempoProcessor` 并保证销毁顺序。
- `ISdkPlaybackSession`、真实 adapter 和 fake session 增加速率 API/event。
- adapter 为未来新建的 session 保存当前合法速率。
- `PlayerEngine` 增加 `Q_PROPERTY(double playbackRate ...)`、设置接口和通知信号。
- `ControlBar` 增加紧凑 ComboBox，选项为 `0.5x` 至 `2.0x` 六个预设。
- 切换处理中避免重复提交；失败时 UI 回退到已确认速率。
- 增加必要翻译、日志与回归检查。

### 测试

- PlayerEngine 属性、幂等设置、失败回退和 session 重建测试。
- QML 组件测试覆盖选择、状态同步、禁用和重新加载。
- fake session 验证事件顺序与异步完成行为。
- offscreen 启动验证 QML 和插件加载。

### 完成标准

- UI 显示的是 SDK 已确认速率，不提前假定切换成功。
- 控件在桌面和窄窗口不遮挡现有播放、进度和音量控件。
- PlayPlugin 组件测试、全量 CTest 和手工播放通过。

### 建议提交

```text
[功能新增] 增加播放倍速控制
```

## 8. R6：完成真实媒体 benchmark 与数据验收

### 目标

用可重复数据确认倍速实现满足时长、音调、同步和性能门禁。

### 改动范围

- 扩展真实媒体 benchmark，支持指定 playback rate 和切速时间点。
- 覆盖 A/V、audio-only、video-only 媒体。
- 增加正弦音频夹具或可重复生成脚本，用于音调验证。
- 输出播放墙钟耗时、媒体时长、A/V drift、late drop、audio underflow、
  CPU 和 RSS。
- 增加播放中切速、暂停切速、seek 后切速和连续切速场景。
- 编写结果文档，记录命令、设备、媒体参数、原始结果和结论。

### 数据门禁

- 墙钟耗时倍率误差不超过 `5%`。
- 正弦主频偏差不超过 `1%`。
- 稳态 A/V drift 不超过 `40 ms` 或一个视频帧时长中的较大值。
- `1.0x` CPU、RSS 和启动延迟相对基线无显著回退。
- 非 `1.0x` 无持续 underflow、异常 late-drop 峰值或内存持续增长。
- 全部场景可重复运行至少三次，并报告中位数和最差值。

### 完成标准

- 结果文档给出逐项 PASS/FAIL，不只描述主观播放感受。
- 失败项必须修复或形成明确的后续任务，不能直接放宽门禁。
- 全量构建、CTest、benchmark 和 offscreen 启动验证通过。

### 建议提交

```text
[测试] 完成倍速播放真实媒体验证
```

## 9. 每任务提交前检查

```bash
git diff --check
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

涉及 QML 的任务额外执行 offscreen 启动；涉及 FFmpeg 资源所有权的任务优先执行
ASan；涉及控制线程、audio thread 和关闭流程的任务增加 TSan 或并发压力验证。
