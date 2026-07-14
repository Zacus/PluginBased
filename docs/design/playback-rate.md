# 真正的倍速播放设计

## 1. 文档状态

- 状态：设计草案
- 目标版本：PlayPlugin / Media SDK 下一阶段
- 实现范围：`0.5x` 到 `2.0x` 的正向倍速播放
- 默认速率：`1.0x`

## 2. 背景

当前播放链路以音频设备时钟作为有声媒体的主时钟，以 `MasterClock` 作为
video-only 媒体的时钟。音频 PCM 经过格式转换后直接写入平台音频输出，视频
调度器按照媒体 PTS 与主时钟的差值决定等待、显示或丢帧。

仅缩短视频等待时间并不是真正的倍速播放：有声媒体会产生音画漂移，直接改变
音频采样率又会改变音调。完整实现必须同时处理音频保调变速、媒体时间推进、
视频调度、动态切速以及 seek/暂停/EOF 等时间线边界。

## 3. 目标

1. 支持 `0.5x`、`0.75x`、`1.0x`、`1.25x`、`1.5x`、`2.0x` 预设速率。
2. 音频变速时保持原始音调。
3. 有声、纯音频和 video-only 媒体都使用同一媒体时间语义。
4. 播放中和暂停中均可切换速率，切换后保持当前位置和原有播放状态。
5. seek、暂停/恢复、EOF、解码回退与倍速状态能够组合工作。
6. `1.0x` 路径不增加音频滤镜延迟和不必要的数据复制。
7. SDK runtime 保持平台无关，不直接依赖 Qt、CoreAudio 或 FFmpeg。

## 4. 非目标

- 不支持倒放、`0x`、大于 `2.0x` 或小于 `0.5x` 的速率。
- 不做运动补偿插帧；低速视频仍按源帧显示。
- 不在本阶段实现跨进程或跨应用启动的速率持久化。
- 不通过丢音频包或重复视频帧模拟倍速。

## 5. 当前链路

```text
ControlBar.qml
    -> PlayerEngine
    -> PlaybackPipeline
    -> SdkPlaybackAdapter
    -> PlaybackSession
       -> Player              (demux / decode / seek)
       -> RuntimePlayer
          -> audio queue -> IAudioOutput
          -> video queue -> AvSyncScheduler -> IVideoPresenter
```

约束如下：

- 有声媒体的 `IAudioOutput::clock()` 是主时钟。
- video-only 时钟当前位于 `AvSyncScheduler` 内部，尚不能作为 session 切速时的
  权威当前位置来源。
- `SessionAudioConverter` 已将音频统一为 Float32、交错排列 PCM，适合作为
  保调变速处理器的输入格式。
- seek 已有 generation 隔离、队列清理和精确预卷机制，可复用为切速时的
  时间线切换基础。

## 6. 总体方案

```text
                                       +-> Player: 精确定位当前媒体时间
PlaybackSession::setPlaybackRate ------+
                                       +-> RuntimePlayer: 新 generation
                                               |
audio queue -> IAudioTempoProcessor -> IAudioOutput(media clock)
video queue -------------------------> AvSyncScheduler(rate aware)
```

采用以下原则：

1. 音频使用 FFmpeg `libavfilter` 的 `atempo` 做保音调变速。
2. 平台音频时钟始终报告“媒体时间”，而不是设备播放时长。
3. 视频等待时间按媒体时间差除以当前速率计算。
4. 动态切速作为一次受控的时间线 discontinuity：清空旧速率缓存，并从当前
   媒体位置精确重建新速率时间线。
5. runtime 只依赖抽象的音频变速接口；FFmpeg 实现在独立具体模块中注入。

## 7. 播放速率契约

对外 API 使用 `double`，统一通过共享校验函数处理：

- 必须为有限值。
- 有效范围为 `[0.5, 2.0]`。
- 与当前值在统一 epsilon 内相等时视为幂等操作。
- SDK 保留有效的任意连续值；首版 UI 只暴露固定预设。

`RuntimePlayerConfig` 增加 `playbackRate`，新建 session 时直接应用。运行中的
`setPlaybackRate()` 表示命令已受理，实际完成通过媒体事件发布。

## 8. 音频保调变速

### 8.1 抽象接口

runtime 增加平台和 FFmpeg 无关的 `IAudioTempoProcessor`。接口至少包含：

```cpp
class IAudioTempoProcessor
{
public:
    virtual ~IAudioTempoProcessor() = default;

    virtual Result<void> configure(const AudioFormat& format,
                                   double playbackRate) = 0;
    virtual Result<AudioTempoOutput> process(const AudioBufferView& input) = 0;
    virtual Result<AudioTempoOutput> drain() = 0;
    virtual void reset() noexcept = 0;
};
```

具体签名以现有 SDK `Result`、buffer 和错误类型为准。契约要求：

- 输入为 Float32、交错排列 PCM。
- `process()` 允许暂时无输出，也允许产生多个输出块。
- 输出块拥有自己的数据，不能引用调用方下一次就会复用的输入存储。
- 输出 PTS 使用媒体时间；处理器从输入 PTS 建立锚点并连续生成输出 PTS。
- `reset()` 丢弃滤镜内部缓存，在 seek、切速、stop 和 generation 变化时调用。
- `drain()` 在音频 EOS 时输出滤镜尾部，完成后才允许发布音频 EOF。
- 处理器只由 runtime audio thread 调用，不要求内部并发调用安全。

### 8.2 FFmpeg 实现

新增具体模块 `sdk/media_audio_ffmpeg`，实现：

```text
abuffer -> atempo=<rate> -> abuffersink
```

该模块显式依赖 `libavfilter`，但 `media_playback_runtime` 不链接 FFmpeg。
PlayPlugin 的 `PlaybackPipeline` 拥有具体处理器并通过 session dependencies 注入，
其生命周期必须覆盖 adapter、session 和 runtime。

`1.0x` 直接旁路处理器，保持现有低延迟路径。非 `1.0x` 且媒体含音频时，如果
没有注入处理器，则拒绝切速；不得静默退化为改变音调或音画不同步的实现。

### 8.3 PTS 与 duration

`atempo` 输出采样率不变，因此输出设备时长与媒体时长的关系为：

```text
mediaDuration = outputDeviceDuration * playbackRate
```

处理器以首个输入 PTS 为锚点，根据已产生的输出 sample 数和速率计算后续媒体
PTS，避免逐块取整造成累计误差。

## 9. 主时钟设计

### 9.1 有声媒体

`AudioBufferView` 增加当前 generation 对应的 `playbackRate`。平台音频输出必须
继续返回媒体时间：

```text
mediaPosition = anchorMediaPts
              + consumedOutputSamples / sampleRate * playbackRate
```

实现应基于锚点和累计消费 sample 数计算，不逐回调累加取整值。这样可避免长时
播放的舍入漂移。

`ClockSnapshot` 字段语义保持明确：

- `position`：媒体时间。
- `queuedDuration`：音频设备时间，不乘播放速率。
- `hardwareLatency`：真实设备延迟，不乘播放速率。
- `generation`：当前 runtime 时间线代际。

同一 generation 内速率必须固定。首次写入建立 PTS、sample cursor 和速率锚点；
同一 generation 出现不同速率时返回错误。切速必须先 flush 并进入新 generation。

### 9.2 Video-only 媒体

video-only 的 `MasterClock` 按以下方式外推媒体时间：

```text
mediaPosition = anchorMediaPts + elapsedWallTime * playbackRate
```

现有平滑与锚点校正继续在媒体时间域工作，切速时重置历史样本。RuntimePlayer
需要对外提供 video-only 权威时钟快照，供 session 捕获切速位置；不得从 QML
显示进度反推位置。

### 9.3 视频调度

`AvSyncScheduler` 接收当前速率。主时钟与视频帧 PTS 都是媒体时间，因此墙钟
等待时间为：

```text
wallWait = (framePts - masterMediaPosition) / playbackRate
```

早到、准时和迟到的阈值应先在媒体时间域比较，或将墙钟阈值按速率换算，确保
不同速率下的丢帧判断语义一致。

## 10. 动态切速

不能只修改原子速率值。音频设备、tempo filter 和帧队列中可能仍有旧速率数据，
继续消费会造成时钟跳变和短暂音画不同步。

运行中切速采用以下流程：

1. session 从 runtime 权威时钟捕获当前媒体位置。
2. 校验新速率及音频处理器能力。
3. runtime 开启新 generation，设置新速率，终止旧队列等待。
4. 清空音视频队列、音频输出、tempo processor、调度器和 EOF 状态。
5. event router 登记 `RateChange` 类型的待完成时间线切换。
6. core 向捕获位置提交一次精确 seek，并保留 playing/paused 状态。
7. seek 预卷完成后，router 建立新的 core/runtime generation 映射。
8. session 发布 `PlaybackRateChangedEvent`，不向上泄漏内部 `SeekCompletedEvent`。

用户 seek 与切速共享统一的 discontinuity 串行化机制。连续快速切速时采用
latest-wins：旧请求的完成事件不能覆盖最新速率或代际。

若 core 重定位失败，session 发布结构化错误并停止或保持暂停，不能在已清空的
新速率 runtime 与旧 core 时间线之间继续播放。

## 11. 状态行为

| 状态 | 切速行为 |
| --- | --- |
| 未打开媒体 | 保存速率，下一次 open 使用 |
| Playing | 从权威当前位置重建时间线，完成后继续播放 |
| Paused | 重建时间线并保持暂停，仅完成目标帧预卷 |
| Seeking | 与 seek 请求串行化，最后一个时间线命令生效 |
| Draining | 清理旧尾部，从当前可定位位置重建 |
| Completed | 保存速率但不隐式 replay；用户 replay/seek 后应用 |
| Error/Stopped | 保存合法配置，不自动启动播放 |

解码器 native fallback 必须继承当前速率和 generation，不得回到 `1.0x`。

## 12. seek、静音和 EOF

### 12.1 seek gap silence

精确 seek 产生的前置静音，其墙钟输出时长应为：

```text
silenceDeviceDuration = mediaGap / playbackRate
```

静音可以直接生成缩放后的 sample 数，无需进入 `atempo`。写入音频输出时仍携带
当前速率，使设备消费后映射回正确的媒体时间。

seek 期间的软件兜底时钟也必须使用 `elapsed * playbackRate` 推进。

### 12.2 EOF

收到音频 EOS 后必须先调用 `drain()`，将所有尾部 PCM 写入音频输出，再等待
设备消费完成并标记音频 EOF。video-only 仍等待最后一帧按倍速调度完成。

## 13. API 与模块改动

### 13.1 SDK

- `RuntimePlayerConfig::playbackRate`
- `RuntimePlayer::setPlaybackRate(double)`
- `RuntimePlayer::clock()` 支持有声和 video-only 权威媒体时钟
- `PlaybackSession::setPlaybackRate(double)` / `playbackRate()`
- `PlaybackSessionDependencies::audioTempoProcessor`
- `RuntimePlayerDependencies::audioTempoProcessor`
- `PlaybackRateChangedEvent`
- `IAudioOutput` 的媒体时钟速率契约

### 13.2 FFmpeg 模块

- 新增 `sdk/media_audio_ffmpeg`
- vcpkg FFmpeg feature 增加 `avfilter`
- CMake 增加 `libavfilter` pkg-config target
- 增加 `FfmpegAudioTempoProcessor`

### 13.3 PlayPlugin

- `PlaybackPipeline` 创建并注入 FFmpeg tempo processor
- `SdkPlaybackAdapter` 保存当前速率，并向新 session 传递
- fake session 同步扩展，保证组件测试可控
- `PlayerEngine` 增加 `playbackRate` 属性、设置接口与变更信号
- `ControlBar` 增加紧凑的速率选择控件

这些公共头文件结构会发生变化。当前 SDK 与插件统一在仓库内重编译，不承诺本次
变更的二进制 ABI 兼容；API 行为和错误语义必须通过测试固定。

## 14. 所有权与线程

| 对象 | 所有者 | 使用线程 | 生命周期要求 |
| --- | --- | --- | --- |
| `FfmpegAudioTempoProcessor` | `PlaybackPipeline` | runtime audio thread | 晚于 session/runtime 销毁 |
| `IAudioTempoProcessor*` | runtime 非拥有引用 | runtime audio thread | 播放期始终有效 |
| 速率控制命令 | session | 调用线程/控制线程 | 与 seek 串行化 |
| CoreAudio rate anchor | audio output | CoreAudio callback | flush/generation 后重建 |
| video-only clock | runtime | video/control thread | 快照读取线程安全 |

音频滤镜绝不在 CoreAudio 实时回调中执行。回调只消费已写入的 ring buffer，并
执行常数时间的媒体时钟换算。

## 15. 错误处理

- 非有限值或越界速率：立即返回参数错误，不改变当前时间线。
- 缺少 tempo processor：有声媒体非 `1.0x` 请求返回能力错误。
- filter configure/process/drain 失败：发布 runtime 错误，停止继续输出不同步内容。
- 同 generation 写入不同速率：视为时间线协议错误。
- 切速内部 seek 失败：切换失败，不发布成功事件。
- `1.0x`：即使未注入 tempo processor 也必须正常工作。

## 16. 诊断指标

建议增加以下统计，便于 benchmark 和现场问题定位：

- 当前 playback rate 与成功切速次数
- tempo 输入/输出 sample 数
- tempo configure/reset/drain/failure 次数
- 切速 discontinuity 次数与耗时
- 不同速率下的音频 underflow、视频 late drop 和最大 A/V drift
- CoreAudio 媒体时钟锚点重建次数

## 17. 验收标准

1. `0.5x` 播放耗时约为 `1.0x` 的两倍，`2.0x` 约为一半，误差不超过 `5%`。
2. 正弦音频夹具在各速率下主频偏差不超过 `1%`，证明音调未随速率变化。
3. 有声媒体稳态 A/V drift 不超过 `40 ms` 或一个视频帧时长中的较大值。
4. video-only 在所有预设速率下位置单调，并在预期墙钟时间内到达 EOF。
5. 播放中、暂停中、seek 后和连续切速均不消费旧 generation 数据。
6. 切速后无永久静音、重复 EOF、卡死或异常 late-drop 峰值。
7. `1.0x` benchmark 相对当前基线无可观测的额外滤镜延迟，CPU/RSS 不显著回退。
8. ASan/TSan 或等价检查下无处理器生命周期、队列并发和关闭顺序问题。

## 18. 风险与对策

| 风险 | 对策 |
| --- | --- |
| `atempo` 内部缓存造成 EOF 截断 | 显式 drain，并测试短音频尾部 |
| 切速时旧 PCM 仍在设备队列 | 新 generation 前 flush，禁止代际内变速 |
| 时钟按回调增量取整产生漂移 | 使用 anchor + 累计 sample cursor 计算 |
| video-only 无 session 级权威位置 | 将 MasterClock 快照提升为 runtime 时钟契约 |
| 快速切速与 seek 交错 | 统一 discontinuity 序列号，latest-wins |
| FFmpeg 依赖侵入 runtime | 独立具体模块，通过接口注入 |
| 错误时静默回退导致音画不同步 | 非 `1.0x` 失败即显式报错并停止输出 |

