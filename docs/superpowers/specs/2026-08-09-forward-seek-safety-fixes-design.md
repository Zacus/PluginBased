# 前进 Seek EOF 与同步错误安全修复设计

## 背景

本轮代码审查确认了两个行为问题：

1. 正向相对 seek 只按 `position < duration` 判断可用性。播放器已进入逻辑 EOF、但最终
   runtime 位置略小于容器时长时，按钮仍可点击，`seekBy()` 也会提交请求。
2. `PlayerEngine::seek()` 在调用 `PlaybackPipeline::seek()` 后执行乐观 UI 更新。SDK
   同步拒绝 seek 时，错误信号会在同一 QObject 线程重入 `onDecoderError()` 并完成重置，
   随后旧的 `seek()` 调用继续写回目标位置，结束态 seek 还可能重新设为 `Playing`。

当前 `SdkPlaybackAdapter` 把 session 事件排入 Qt 对象线程，但同步 API 错误直接发信号；
因此同步错误和异步完成必须分别处理，不能通过提前发送 UI 信号规避。

## 目标

- 逻辑 EOF 时立即禁用“前进 3 秒”，引擎层同时拒绝正向相对 seek。
- 保留绝对向后 seek 和负增量相对 seek 从 EOF 恢复播放的既有能力。
- 同步 seek 错误重置后，旧调用不能再次写入位置或播放状态。
- 保持连续前进基于最新 pending 目标累加，并仅由最新 generation 完成事件解除 pending。
- 不增加跨线程同步等待，不移动 UI 通知到 seek 提交之前。

## 非目标

- 不改变 SDK seek API、`PlaybackPipeline`/`SdkPlaybackAdapter` 的跨线程接口或错误类型。
- 不修复本轮审查中的 Minor 项。
- 不重构播放器整体依赖注入或播放完成状态机。

## 当前调用链与线程边界

```text
QML forwardRequested
  -> PlayerEngine::seekBy(3000)            Qt/QML 对象线程
  -> PlayerEngine::seek(target)
  -> PlaybackPipeline::seek(...)
  -> SdkPlaybackAdapter::seek(...)
       -> session->seek(...)               同一对象线程发起
       -> 同步失败: errorOccurred          直接信号重入 PlayerEngine
       -> 异步完成: postSessionEvent        QueuedConnection 回对象线程
```

`PlaybackSeekState` 是 `PlayerEngine` 的值成员，只在 `PlayerEngine` 所属线程访问；本次不
增加锁、引用、原生资源或新的所有权关系。

## 设计

### 权威的前进可用状态

`PlayerEngine` 新增只读 QML 属性 `canSeekForward`。其值只在以下条件全部成立时为真：

- 已有 `MediaInfo`；
- `duration > 0`；
- `position < duration`；
- `PlaybackCompletionTracker::isMediaFinished()` 为假。

状态由私有 `refreshCanSeekForward()` 在媒体打开/停止、媒体信息到达、错误、位置变化、
乐观 seek 更新和 `finishMedia()` 后重算。只有布尔值真实变化时才发
`canSeekForwardChanged`，所有修改均发生在 `PlayerEngine` 所属线程。

`ControlBar` 不再自行推断 EOF，而是由 `PlayerView` 绑定
`canSeekForward: engine.canSeekForward`。`seekBy(deltaMs)` 对正增量再次检查该权威状态；
负增量不受 `canSeekForward` 限制，继续交给原有边界计算和绝对 seek 恢复逻辑。

纯函数 `isForwardSeekAvailable(hasMedia, position, duration, mediaFinished)` 放在
`PlaybackSeek` 中，使逻辑 EOF、无媒体、未知时长和数值 EOF 可独立进行行为测试。

### 同步错误后的旧调用失效

`PlaybackSeekState` 新增 `isPending(generation)`，仅当指定 generation 仍是当前 pending
请求时返回真。

`PlayerEngine::seek()` 保持现有顺序：先 `begin()`，再调用管线。管线返回后立即检查
`isPending(seekGeneration)`：

- 同步错误已经重入 `onDecoderError()` 并调用 `reset()`，检查失败，旧调用直接返回；
- 正常提交时 pending 仍属于该 generation，才更新 UI 位置及 EOF 恢复状态；
- SDK 完成事件仍由 queued connection 稍后处理，不会在正常同步返回前清除 pending。

此设计不提前发射 `positionChanged`/`playbackStateChanged`，避免在实际 dispatch 之前引入
新的用户代码重入窗口；也不把异步 adapter 接口改造成同步返回接口。

## API、ABI 与兼容性

- 新增的是 PlayPlugin 内部 QML 类型的只读属性和通知信号，不修改 `IAppPlugin` ABI。
- `PlaybackSeekState::isPending()` 和纯函数属于插件内部 C++ 接口，目标标准保持 C++17。
- 不改变正向 3 秒增量、倍速标签、seek generation 编号或 SDK timeline generation。

## 错误、停止与关闭行为

- open、stop、error 继续通过 `PlaybackSeekState::reset()` 使所有旧调用失效。
- 同步错误重置是最终状态；旧调用不得覆盖 `Stopped`、位置 0 或时长 0。
- 异步错误仍按现有信号链停止组件并重置状态。
- 不增加阻塞、等待、跨线程直接调用或新的析构顺序。

## 备选方案

- EOF 时把位置强制改成 duration：修改较少，但把逻辑完成与时间值混为一谈，仍可能被
  后续位置事件覆盖。
- 把乐观 UI 更新移到管线调用前：能让同步错误最后执行，但会在真正提交 seek 前发出
  QML 信号，扩大重入窗口。
- 让 adapter/pipeline 返回同步 `Result`：接口更显式，但与现有跨线程 queued 调用方式
  冲突，修改范围和回归风险明显更大。

## 验证

- TDD 增加逻辑 EOF 但 `position < duration` 时不可前进的纯行为测试。
- TDD 增加 generation 在 `reset()` 后不再 pending、陈旧完成不影响最新 pending 的测试。
- 回归契约检查验证 QML 绑定权威属性，并验证管线调用、pending 检查、UI 更新的顺序。
- 运行 `PlayPluginPlaybackSeekTest` 和相关 PlayPlugin 聚焦 CTest。
- 运行 Debug 全量构建、58 项 CTest 和 `git diff --check`。
- 人工验证普通暂停仍可前进、逻辑 EOF 禁用、绝对后退 seek 可从 EOF 恢复、同步错误后
  UI 不回跳且状态不复活。

## 剩余风险

当前 `PlayerEngine` 直接构造具体 `PlaybackPipeline`，无法在不扩大生产架构的前提下为
同步错误注入完整 fake pipeline。本轮用已测试的 pending token 行为和调用顺序契约覆盖
该重入防线；真实错误 UI 表现仍需人工验证。
