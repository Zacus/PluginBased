# Forward Seek Safety Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让逻辑 EOF 的前进按钮和引擎请求同时失效，并保证同步 seek 错误重置不会被旧调用覆盖。

**Architecture:** `PlayerEngine` 维护并向 QML 暴露权威的 `canSeekForward` 状态，状态计算下沉为可单测纯函数。同步错误继续沿现有直接信号链重置状态，`PlaybackSeekState` 的 generation pending token 负责让返回后的旧 `seek()` 调用失效，不改变 adapter/pipeline 的线程或返回接口。

**Tech Stack:** C++17、Qt 6.8.3、QML、Python 3 回归契约检查、CMake/Ninja、CTest。

## Global Constraints

- 逻辑 EOF 时立即禁用“前进 3 秒”，引擎层同时拒绝正向相对 seek。
- 保留绝对向后 seek 和负增量相对 seek 从 EOF 恢复播放的既有能力。
- 同步 seek 错误重置后，旧调用不能再次写入位置或播放状态。
- 保持连续前进基于最新 pending 目标累加，并仅由最新 generation 完成事件解除 pending。
- 不增加跨线程同步等待，不移动 UI 通知到 seek 提交之前。
- 不修改 SDK seek API、`PlaybackPipeline`/`SdkPlaybackAdapter` 的跨线程接口或错误类型。
- 当前工作区已有未提交功能；未经用户明确要求，不创建 Git 提交。

---

### Task 1: 建立逻辑 EOF 的权威前进状态

**Files:**
- Modify: `plugins/PlayPlugin/src/playback/PlaybackSeek.h:7-24`
- Modify: `plugins/PlayPlugin/src/playback/PlaybackSeek.cpp:6-63`
- Modify: `tests/tst_playplugin_playback_seek.cpp:6-83`
- Modify: `plugins/PlayPlugin/src/playback/PlayerEngine.h:34-130`
- Modify: `plugins/PlayPlugin/src/playback/PlayerEngine.cpp:101-363`
- Modify: `plugins/PlayPlugin/qml/ControlBar.qml:11-22`
- Modify: `plugins/PlayPlugin/qml/PlayerView.qml:120-135`
- Modify: `tests/playplugin_regression_checks.py:17-120`

**Interfaces:**
- Produces: `bool isForwardSeekAvailable(bool hasMedia, qint64 currentPositionMs, qint64 durationMs, bool mediaFinished)`。
- Produces: `Q_PROPERTY(bool canSeekForward READ canSeekForward NOTIFY canSeekForwardChanged)`。
- Preserves: `seekBy(deltaMs < 0)` 可在 EOF 后进入原有绝对 seek 恢复路径。

- [ ] **Step 1: 写入失败的逻辑 EOF 行为测试**

在 `tests/tst_playplugin_playback_seek.cpp` 增加：

```cpp
void forwardAvailabilityIncludesLogicalCompletion()
{
    assert(isForwardSeekAvailable(true, 12'000, 60'000, false));
    assert(!isForwardSeekAvailable(false, 12'000, 60'000, false));
    assert(!isForwardSeekAvailable(true, 12'000, 0, false));
    assert(!isForwardSeekAvailable(true, 60'000, 60'000, false));
    assert(!isForwardSeekAvailable(true, 59'500, 60'000, true));
}
```

并在 `main()` 调用 `forwardAvailabilityIncludesLogicalCompletion()`。

在 `tests/playplugin_regression_checks.py` 增加 QML/引擎接线契约：

```python
    require("Q_PROPERTY(bool       canSeekForward" in engine_h
            and "canSeekForwardChanged" in engine_h
            and "refreshCanSeekForward" in engine_cpp
            and "deltaMs > 0 && !m_canSeekForward" in engine_cpp,
            "PlayerEngine should expose authoritative forward-seek availability")
    require("property bool canSeekForward: false" in control_bar_qml
            and "readonly property bool canSeekForward" not in control_bar_qml
            and "canSeekForward: engine.canSeekForward"
                in read("plugins/PlayPlugin/qml/PlayerView.qml"),
            "ControlBar should consume PlayerEngine forward-seek availability")
```

- [ ] **Step 2: 运行测试并确认旧实现失败**

Run: `cmake --build --preset debug --target PlayPluginPlaybackSeekTest --parallel`

Expected: FAIL，`isForwardSeekAvailable` 尚未声明。

Run: `python3 tests/playplugin_regression_checks.py`

Expected: FAIL，消息为 `PlayerEngine should expose authoritative forward-seek availability`。

- [ ] **Step 3: 实现可测试的前进可用性规则**

在 `PlaybackSeek.h` 声明：

```cpp
[[nodiscard]] bool isForwardSeekAvailable(
    bool hasMedia,
    qint64 currentPositionMs,
    qint64 durationMs,
    bool mediaFinished);
```

在 `PlaybackSeek.cpp` 实现：

```cpp
bool isForwardSeekAvailable(
    bool hasMedia,
    qint64 currentPositionMs,
    qint64 durationMs,
    bool mediaFinished)
{
    return hasMedia
        && durationMs > 0
        && currentPositionMs < durationMs
        && !mediaFinished;
}
```

- [ ] **Step 4: 在 PlayerEngine 中维护权威属性**

在 `PlayerEngine.h` 增加 Q_PROPERTY、getter、signal、刷新函数和缓存值：

```cpp
    Q_PROPERTY(bool       canSeekForward READ canSeekForward NOTIFY canSeekForwardChanged)

    bool canSeekForward() const { return m_canSeekForward; }

    void canSeekForwardChanged(bool canSeekForward);

    void refreshCanSeekForward();

    bool          m_canSeekForward = false;
```

在 `PlayerEngine.cpp` 增加：

```cpp
void PlayerEngine::refreshCanSeekForward()
{
    const bool available = isForwardSeekAvailable(
        m_mediaInfo != nullptr,
        m_position,
        m_duration,
        m_completion.isMediaFinished());
    if (m_canSeekForward == available)
        return;

    m_canSeekForward = available;
    emit canSeekForwardChanged(m_canSeekForward);
}
```

在 open reset、stop、media info ready、decoder error、三个位置处理函数、成功乐观 seek 更新和
`finishMedia()` 的状态写入后调用 `refreshCanSeekForward()`。在 `seekBy()` 开头加入：

```cpp
    if (!m_mediaInfo || (deltaMs > 0 && !m_canSeekForward))
        return;
```

正增量因此在逻辑 EOF 被拒绝，负增量仍进入现有边界计算。

- [ ] **Step 5: 把 QML 按钮绑定到权威属性**

在 `ControlBar.qml` 把本地推导改为输入属性：

```qml
    property bool canSeekForward: false
```

在 `PlayerView.qml` 的 `ControlBar` 绑定中加入：

```qml
        canSeekForward: engine.canSeekForward
```

- [ ] **Step 6: 运行聚焦测试并确认通过**

Run: `cmake --build --preset debug --target PlayPluginPlaybackSeekTest PlayPlugin --parallel`

Expected: 退出码 0。

Run: `python3 tests/playplugin_regression_checks.py`

Expected: PASS，退出码 0。

Run: `ctest --test-dir build --output-on-failure -R 'playplugin_(regression_checks|playback_seek)'`

Expected: 2/2 通过。

### Task 2: 让同步错误使旧 seek 调用失效

**Files:**
- Modify: `plugins/PlayPlugin/src/playback/PlaybackSeek.h:12-25`
- Modify: `plugins/PlayPlugin/src/playback/PlaybackSeek.cpp:28-63`
- Modify: `tests/tst_playplugin_playback_seek.cpp:40-90`
- Modify: `plugins/PlayPlugin/src/playback/PlayerEngine.cpp:158-185`
- Modify: `tests/playplugin_regression_checks.py:30-50`

**Interfaces:**
- Produces: `[[nodiscard]] bool PlaybackSeekState::isPending(int generation) const`。
- Consumes: open、stop、error 已有的 `PlaybackSeekState::reset()`。
- Preserves: pipeline dispatch 发生在乐观 UI 更新之前；不会提前发 QML 通知。

- [ ] **Step 1: 写入失败的 pending token 测试**

在 `tests/tst_playplugin_playback_seek.cpp` 增加：

```cpp
void resetInvalidatesInFlightGeneration()
{
    PlaybackSeekState state;
    const int generation = state.begin(22'000);
    assert(state.isPending(generation));

    state.reset();

    assert(!state.isPending(generation));
}
```

并在 `main()` 调用 `resetInvalidatesInFlightGeneration()`。

在 `tests/playplugin_regression_checks.py` 中提取 `PlayerEngine::seek()` 函数体并检查顺序：

```python
    seek_body = engine_cpp[
        engine_cpp.find("void PlayerEngine::seek(qint64 positionMs)"):
        engine_cpp.find("void PlayerEngine::seekBy(qint64 deltaMs)")]
    dispatch_index = seek_body.find("m_pipeline->seek(targetPositionMs, seekGeneration, resumeAfterSeek)")
    pending_guard_index = seek_body.find("!m_seekState.isPending(seekGeneration)")
    optimistic_update_index = seek_body.find("m_position = targetPositionMs")
    require(-1 not in (dispatch_index, pending_guard_index, optimistic_update_index)
            and dispatch_index < pending_guard_index < optimistic_update_index,
            "PlayerEngine should ignore optimistic updates invalidated by synchronous seek errors")
```

- [ ] **Step 2: 运行测试并确认旧实现失败**

Run: `cmake --build --preset debug --target PlayPluginPlaybackSeekTest --parallel`

Expected: FAIL，`PlaybackSeekState` 尚无 `isPending`。

Run: `python3 tests/playplugin_regression_checks.py`

Expected: FAIL，消息为 `PlayerEngine should ignore optimistic updates invalidated by synchronous seek errors`。

- [ ] **Step 3: 实现 generation pending 查询**

在 `PlaybackSeek.h` 声明：

```cpp
    [[nodiscard]] bool isPending(int generation) const;
```

在 `PlaybackSeek.cpp` 实现：

```cpp
bool PlaybackSeekState::isPending(int generation) const
{
    return m_pendingPosition.has_value() && generation == m_pendingGeneration;
}
```

- [ ] **Step 4: 在管线返回后阻止失效调用写回**

保持 dispatch 的当前位置，在其后立即加入：

```cpp
    m_pipeline->seek(targetPositionMs, seekGeneration, resumeAfterSeek);
    if (!m_seekState.isPending(seekGeneration))
        return;

    // 立即更新 UI 进度条（不等音频时钟重建）
```

同步错误重入 `onDecoderError()` 后会先执行 `reset()`，因此返回后的旧调用无法更新位置或
恢复 `Playing`。不移动任何 UI signal 到 dispatch 之前。

- [ ] **Step 5: 运行聚焦测试并确认通过**

Run: `cmake --build --preset debug --target PlayPluginPlaybackSeekTest PlayPlugin --parallel`

Expected: 退出码 0。

Run: `python3 tests/playplugin_regression_checks.py`

Expected: PASS，退出码 0。

Run: `ctest --test-dir build --output-on-failure -R 'playplugin_(regression_checks|playback_seek|pending_seek_requests|sdk_playback_adapter)'`

Expected: 4/4 通过。

### Task 3: 全量验证与新增风险复审

**Files:**
- Verify: `plugins/PlayPlugin/src/playback/PlaybackSeek.h`
- Verify: `plugins/PlayPlugin/src/playback/PlaybackSeek.cpp`
- Verify: `plugins/PlayPlugin/src/playback/PlayerEngine.h`
- Verify: `plugins/PlayPlugin/src/playback/PlayerEngine.cpp`
- Verify: `plugins/PlayPlugin/qml/ControlBar.qml`
- Verify: `plugins/PlayPlugin/qml/PlayerView.qml`
- Verify: `tests/tst_playplugin_playback_seek.cpp`
- Verify: `tests/playplugin_regression_checks.py`

**Interfaces:**
- Consumes: Task 1 的权威 EOF 状态和 Task 2 的 pending generation guard。
- Produces: 新鲜的构建、测试、差异和独立复审证据。

- [ ] **Step 1: 运行 Debug 全量构建和测试**

Run: `cmake --build --preset debug --parallel`

Expected: 退出码 0。

Run: `ctest --preset debug`

Expected: 58/58 通过。受限沙箱若触发 Qt ARM64/NEON 误报，使用已批准的沙箱外同命令复跑。

- [ ] **Step 2: 检查差异质量和范围**

Run: `git diff --check`

Expected: 退出码 0。

Run: `git diff -- plugins/PlayPlugin/src/playback/PlaybackSeek.h plugins/PlayPlugin/src/playback/PlaybackSeek.cpp plugins/PlayPlugin/src/playback/PlayerEngine.h plugins/PlayPlugin/src/playback/PlayerEngine.cpp plugins/PlayPlugin/qml/ControlBar.qml plugins/PlayPlugin/qml/PlayerView.qml tests/tst_playplugin_playback_seek.cpp tests/playplugin_regression_checks.py`

Expected: 不包含 SDK/pipeline 接口修改、提前 UI 通知、跨线程等待或无关重构。

- [ ] **Step 3: 逐项复审关键状态转换**

确认以下契约均能从代码和测试定位：

```text
normal forward: begin -> dispatch -> pending true -> optimistic update
synchronous error: begin -> dispatch -> error/reset -> pending false -> return
logical EOF: finish -> canSeekForward false -> QML disabled -> positive seekBy rejected
negative EOF seek: positive-only guard skipped -> absolute seek recovery path preserved
open/stop/error: reset pending and refresh canSeekForward on PlayerEngine thread
```

- [ ] **Step 4: 请求独立只读代码复审**

审查范围限定为本计划修改文件，重点寻找 Critical：QObject 重入、状态复活、通知遗漏、
generation 误清、EOF 后退 seek 回归、ABI/线程边界变化。若发现 Critical 或 Important，停止
完成声明并先修复、复测。

- [ ] **Step 5: 记录人工 UI 验证状态**

在可见桌面和真实媒体下验证：普通暂停仍可前进；逻辑 EOF 即使 position 小于 duration，
按钮也禁用；绝对向后 seek 可恢复；同步错误后位置保持 0、时长保持 0、状态保持 Stopped。

若当前环境无法可靠观察原生 GUI，最终明确报告该项未验证。
