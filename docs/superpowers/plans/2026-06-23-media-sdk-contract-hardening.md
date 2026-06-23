# Media SDK Contract Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Harden the PlayPlugin/media_sdk playback contracts so seek, EOF, pause, queue backpressure, and cross-thread delivery are deterministic before adding more SDK features.

**Architecture:** Keep `media_sdk_core` focused on demux/decode/frame/event output. Move playback timing, render/wait/drop decisions, and GUI updates to the Qt presenter/renderer side. Route data events through a non-GUI bridge with generation filtering, cancellable backpressure, and formal drain semantics.

**Tech Stack:** C++20 SDK core, C++17 Qt 6 PlayPlugin, FFmpeg, CMake/CTest, Qt `QThread`/`QTimer` only in PlayPlugin, `std::jthread`/fallback worker in SDK.

---

## Non-Negotiable Rules

- Do not add another temporary timer retry for EOF.
- Do not fix early EOF by increasing queue sizes.
- Do not silently drop audio frames in normal playback.
- Do not let `QtPlaybackAdapter` drop normal video frames because a queue is full.
- Do not block the GUI thread on `FrameQueue::push()`.
- Do not emit `seekCompleted` until the SDK worker has actually completed seek and decoder flush.
- Do not let old open/seek events be relabeled with the current serial.
- Every phase below ends with a build/test gate and one commit.

## File Responsibility Map

- `sdk/media_core/include/media_sdk/MediaEvents.h`: public event payloads, event metadata, generation/session contract.
- `sdk/media_core/src/DecodeWorker.h/.cpp`: SDK worker session/generation ownership, true seek completion, event metadata stamping.
- `sdk/media_core/tests/tst_media_core_frame_contract.cpp`: compile-time and value tests for event metadata and payload contracts.
- `sdk/media_core/tests/tst_media_core_playback_worker.cpp`: integration tests for real seek completion, generation progression, EOF order, stop behavior.
- `plugins/PlayPlugin/src/common/FrameQueue.h`: Qt-side bounded queue and formal EOS/drain API.
- `plugins/PlayPlugin/src/playback/PlaybackDataBridge.h/.cpp`: non-GUI data bridge that converts SDK frames to Qt frames and pushes data/EOF with cancellable backpressure.
- `plugins/PlayPlugin/src/playback/QtPlaybackAdapter.h/.cpp`: control-event marshal, data-event routing, generation filtering, no GUI data enqueue.
- `plugins/PlayPlugin/src/playback/PlaybackPipeline.cpp`: stop/open/seek cancellation order and queue abort/reset sequencing.
- `plugins/PlayPlugin/src/audio/AudioRenderer.h/.cpp`: audio parameter data-race fix.
- `plugins/PlayPlugin/src/video/VideoRenderer.cpp`: consume formal EOS entries and preserve presenter-side drop policy.
- `tests/playplugin_qt_playback_adapter_checks.py`: architecture checks for adapter data path constraints.
- `tests/playplugin_bplus_completion_checks.py`: regression checks for EOF timer retry removal and drain semantics.
- `plugins/PlayPlugin/CMakeLists.txt`: add new PlayPlugin bridge files.

## Verification Commands

Use the smallest relevant command inside each task, then the broader gate before committing:

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_core|playplugin_qt_playback_adapter_checks|playplugin_bplus_completion_checks"
ctest --test-dir build --output-on-failure
```

If the existing build directory is missing, configure first with the repository's normal command from `AGENTS.md`.

---

### Task 1: Add SDK Event Session/Generation Contract

**Purpose:** Prevent old asynchronous events from being accepted by a new open/seek generation.

**Files:**
- Modify: `sdk/media_core/include/media_sdk/MediaEvents.h`
- Modify: `sdk/media_core/src/DecodeWorker.h`
- Modify: `sdk/media_core/src/DecodeWorker.cpp`
- Modify: `sdk/media_core/tests/tst_media_core_frame_contract.cpp`
- Modify: `sdk/media_core/tests/tst_media_core_playback_worker.cpp`

- [ ] **Step 1: Add failing metadata assertions**

Add tests that require every `PlayerEvent` to carry metadata:

```cpp
media_sdk::PlayerEvent event {
    .metadata = { .sessionId = 7, .generation = 3 },
    .payload = media_sdk::StateChangedEvent { media_sdk::PlayerState::Playing },
};
assert(event.metadata.sessionId == 7);
assert(event.metadata.generation == 3);
```

In playback worker tests, capture media info, one frame, seek completion, and EOF events; assert all events from the same open have the same `sessionId`, and events after seek have a greater or equal `generation`.

- [ ] **Step 2: Run the focused tests and verify failure**

Run:

```bash
cmake --build build --target MediaSdkCoreFrameContractTest MediaSdkCorePlaybackWorkerTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_core_frame_contract|media_sdk_core_playback_worker"
```

Expected: compile failure or assertion failure because `PlayerEvent::metadata` does not exist yet.

- [ ] **Step 3: Implement event metadata**

In `MediaEvents.h`, add:

```cpp
struct EventMetadata {
    std::uint64_t sessionId = 0;
    std::uint64_t generation = 0;
};

struct PlayerEvent {
    EventMetadata metadata;
    PlayerEventPayload payload;
};
```

Update all direct `PlayerEvent` construction to include metadata through `DecodeWorker::makeEvent(...)`, not ad hoc aggregate initializers in production code.

- [ ] **Step 4: Stamp metadata in `DecodeWorker`**

Add worker members:

```cpp
std::uint64_t m_sessionId = 0;
std::uint64_t m_generation = 0;
```

Rules:
- Increment `m_sessionId` on successful open before emitting `MediaInfoEvent`.
- Reset `m_generation = 0` on successful open.
- Increment `m_generation` only after successful seek and decoder flush.
- `stop`/`closeMedia` must not reuse old queued events as current events.

Add helper:

```cpp
PlayerEvent DecodeWorker::makeEvent(PlayerEventPayload payload) const
{
    return PlayerEvent {
        .metadata = { .sessionId = m_sessionId, .generation = m_generation },
        .payload = std::move(payload),
    };
}
```

- [ ] **Step 5: Verify tests pass**

Run:

```bash
cmake --build build --target MediaSdkCoreFrameContractTest MediaSdkCorePlaybackWorkerTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_core_frame_contract|media_sdk_core_playback_worker"
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add sdk/media_core/include/media_sdk/MediaEvents.h sdk/media_core/src/DecodeWorker.h sdk/media_core/src/DecodeWorker.cpp sdk/media_core/tests/tst_media_core_frame_contract.cpp sdk/media_core/tests/tst_media_core_playback_worker.cpp
git commit -m "[功能修改] 增加媒体事件代际契约"
```

---

### Task 2: Replace Fake Seek Completion With Worker Seek Completion

**Purpose:** Make `seekCompleted` mean actual worker completion, not command submission.

**Files:**
- Modify: `sdk/media_core/include/media_sdk/MediaEvents.h`
- Modify: `sdk/media_core/src/DecodeWorker.cpp`
- Modify: `sdk/media_core/tests/tst_media_core_playback_worker.cpp`
- Modify: `plugins/PlayPlugin/src/playback/QtPlaybackAdapter.h`
- Modify: `plugins/PlayPlugin/src/playback/QtPlaybackAdapter.cpp`

- [ ] **Step 1: Add failing seek completion test**

Extend worker test to require `SeekCompletedEvent`:

```cpp
bool hasSeekCompletedAtOrAfter(const media_sdk::PlayerEvent& event,
                               std::chrono::milliseconds position)
{
    if (const auto* payload = std::get_if<media_sdk::SeekCompletedEvent>(&event.payload))
        return payload->position >= position;
    return false;
}
```

After `player.seek(100ms)`, wait for `SeekCompletedEvent` and assert its metadata generation is greater than the pre-seek generation.

- [ ] **Step 2: Run focused tests and verify failure**

Run:

```bash
cmake --build build --target MediaSdkCorePlaybackWorkerTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_core_playback_worker"
```

Expected: compile failure because `SeekCompletedEvent` does not exist.

- [ ] **Step 3: Add `SeekCompletedEvent`**

In `MediaEvents.h`:

```cpp
struct SeekCompletedEvent {
    std::chrono::milliseconds position { 0 };
};
```

Add it to `PlayerEventPayload`.

- [ ] **Step 4: Emit from `DecodeWorker::handleSeek()`**

After `av_seek_frame` succeeds and both codec buffers are flushed:

```cpp
++m_generation;
m_clock.invalidate();
emitEvent(makeEvent(SeekCompletedEvent { position }));
emitEvent(makeEvent(PositionChangedEvent { position }));
```

The exact order should be seek completion first, then position update, so Qt can safely release seek gating before UI progress catches up.

- [ ] **Step 5: Remove fake Qt seek completion**

In `QtPlaybackAdapter::seekTo(...)`, remove immediate:

```cpp
emit seekCompleted(m_pendingSeekGeneration, m_currentSerial);
```

Instead, store Qt request ids in FIFO:

```cpp
std::deque<int> m_pendingSeekRequests;
```

On `SeekCompletedEvent`, pop the oldest pending Qt request id and emit:

```cpp
emit seekCompleted(qtGeneration, static_cast<int>(event.metadata.generation));
```

If no pending request exists, ignore the stale completion and log at debug level.

- [ ] **Step 6: Verify focused tests**

Run:

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_core_playback_worker|playplugin_qt_playback_adapter_checks"
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add sdk/media_core/include/media_sdk/MediaEvents.h sdk/media_core/src/DecodeWorker.cpp sdk/media_core/tests/tst_media_core_playback_worker.cpp plugins/PlayPlugin/src/playback/QtPlaybackAdapter.h plugins/PlayPlugin/src/playback/QtPlaybackAdapter.cpp
git commit -m "[功能修复] 使用真实seek完成事件"
```

---

### Task 3: Add Formal EOF Drain API To Qt FrameQueue

**Purpose:** Stop representing EOF as an incidental `nullptr` pushed by retry timer.

**Files:**
- Modify: `plugins/PlayPlugin/src/common/FrameQueue.h`
- Modify: `tests/playplugin_bplus_completion_checks.py`

- [ ] **Step 1: Add architecture regression check**

In `tests/playplugin_bplus_completion_checks.py`, add checks:

```python
assert "finish(" in frame_queue_text or "pushEndOfStream(" in frame_queue_text
assert "QTimer::singleShot(10" not in adapter_text
assert "tryPush(nullptr" not in adapter_text
```

For this task, the timer assertions may remain failing until Task 5. The `FrameQueue` API assertion must pass in this task.

- [ ] **Step 2: Add `FrameQueue::finish()`**

In `FrameQueue.h`, add:

```cpp
bool finish(int serial = 0)
{
    return push(T {}, serial, true);
}

bool tryFinish(int serial = 0)
{
    return tryPush(T {}, serial, true);
}
```

Keep `Entry::eof` for compatibility, but all new code must call `finish()` rather than spelling `nullptr, eof=true`.

- [ ] **Step 3: Add drain behavior test**

If adding a C++ PlayPlugin queue test is too invasive, add a Python check that validates `finish()` uses blocking `push`, not `tryPush`, for the normal path.

Expected source invariant:

```cpp
bool finish(int serial = 0)
{
    return push(T {}, serial, true);
}
```

- [ ] **Step 4: Verify focused checks**

Run:

```bash
ctest --test-dir build --output-on-failure -R "playplugin_bplus_completion_checks"
```

Expected: this may still fail on timer/adapter assertions until Task 5. The failure must be limited to adapter EOF retry references, not missing `finish()`.

- [ ] **Step 5: Commit only if the check state is understood**

If the repository policy requires all committed tests pass, defer adding the failing timer assertions to Task 5. Commit the queue API and non-failing check now:

```bash
git add plugins/PlayPlugin/src/common/FrameQueue.h tests/playplugin_bplus_completion_checks.py
git commit -m "[功能修改] 增加帧队列EOF排空接口"
```

---

### Task 4: Add Non-GUI Playback Data Bridge

**Purpose:** Move audio/video frame enqueue and EOF enqueue out of GUI-thread `handleEvent()`.

**Files:**
- Create: `plugins/PlayPlugin/src/playback/PlaybackDataBridge.h`
- Create: `plugins/PlayPlugin/src/playback/PlaybackDataBridge.cpp`
- Modify: `plugins/PlayPlugin/CMakeLists.txt`
- Modify: `plugins/PlayPlugin/src/playback/QtPlaybackAdapter.h`
- Modify: `plugins/PlayPlugin/src/playback/QtPlaybackAdapter.cpp`
- Modify: `tests/playplugin_qt_playback_adapter_checks.py`

- [ ] **Step 1: Add adapter architecture checks**

Checks should enforce:

```python
assert "AudioFrameEvent" not in gui_handle_event_body or "tryPush" not in gui_handle_event_body
assert "VideoFrameEvent" not in gui_handle_event_body or "tryPush" not in gui_handle_event_body
assert "m_paused" not in audio_frame_data_path
assert "m_paused" not in video_frame_data_path
assert "PlaybackDataBridge" in adapter_text
```

- [ ] **Step 2: Create bridge interface**

`PlaybackDataBridge` should be a non-`QObject` class. It observes queues and owns conversion logic or delegates conversion callbacks.

Minimum public API:

```cpp
class PlaybackDataBridge final
{
public:
    struct StreamState {
        bool hasAudio = false;
        bool hasVideo = false;
        std::uint64_t sessionId = 0;
        std::uint64_t generation = 0;
    };

    PlaybackDataBridge(VideoFrameQueue* videoQueue, AudioFrameQueue* audioQueue);

    void reset(StreamState state);
    void cancel();
    bool pushAudio(AVFramePtr frame, std::uint64_t sessionId, std::uint64_t generation);
    bool pushVideo(AVFramePtr frame, std::uint64_t sessionId, std::uint64_t generation);
    bool finish(std::uint64_t sessionId, std::uint64_t generation);
};
```

Rules:
- `pushAudio()` uses blocking `AudioFrameQueue::push()`.
- `pushVideo()` uses blocking `VideoFrameQueue::push()` for this hardening phase.
- `finish()` uses `FrameQueue::finish()`.
- All methods reject mismatched session/generation before touching queues.
- No Qt GUI calls inside this class.

- [ ] **Step 3: Route data events from `QtPlaybackAdapter::onEvent()`**

In `onEvent()`:
- If payload is `AudioFrameEvent`, convert and call bridge directly on the SDK callback thread.
- If payload is `VideoFrameEvent`, convert and call bridge directly on the SDK callback thread.
- If payload is `EndOfFileEvent`, call bridge `finish(...)` directly, then queue only the Qt `endOfFile()` signal.
- All control events continue through queued Qt invocation.

`handleEvent()` should no longer contain normal audio/video frame queue writes.

- [ ] **Step 4: Remove pause-time data dropping**

Delete data-path checks that return when `m_paused` is true. Pause belongs to audio/video renderers; adapter/data bridge must not drop decoded data because playback is paused.

- [ ] **Step 5: Verify focused checks**

Run:

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R "playplugin_qt_playback_adapter_checks|playplugin_bplus_completion_checks"
```

Expected: PASS for adapter data-path checks.

- [ ] **Step 6: Commit**

```bash
git add plugins/PlayPlugin/CMakeLists.txt plugins/PlayPlugin/src/playback/PlaybackDataBridge.h plugins/PlayPlugin/src/playback/PlaybackDataBridge.cpp plugins/PlayPlugin/src/playback/QtPlaybackAdapter.h plugins/PlayPlugin/src/playback/QtPlaybackAdapter.cpp tests/playplugin_qt_playback_adapter_checks.py
git commit -m "[功能修改] 引入非GUI播放数据桥"
```

---

### Task 5: Remove EOF Timer Retry And Adapter Queue Dropping

**Purpose:** Make EOF ordering a queue/drain property, not a retry-timer workaround.

**Files:**
- Modify: `plugins/PlayPlugin/src/playback/QtPlaybackAdapter.h`
- Modify: `plugins/PlayPlugin/src/playback/QtPlaybackAdapter.cpp`
- Modify: `tests/playplugin_bplus_completion_checks.py`
- Modify: `plugins/PlayPlugin/PlayPluginExecutionFlow.md` if current implementation notes change.

- [ ] **Step 1: Add failing checks for removed temporary EOF path**

Checks must enforce absence of:

```text
m_pendingAudioEof
m_pendingVideoEof
m_eofRetryScheduled
tryPushPendingEof
scheduleEofRetry
QTimer::singleShot
tryPush(nullptr
dropped audio frame because queue is full
dropped video frame because queue is full
```

- [ ] **Step 2: Delete temporary EOF state**

Remove from header and implementation:
- pending EOF booleans.
- retry scheduled boolean.
- `clearPendingEof()`.
- `tryPushPendingEof()`.
- `scheduleEofRetry()`.
- `#include <QTimer>`.

- [ ] **Step 3: Ensure EOF event order**

EOF handling must be:

```cpp
if (std::holds_alternative<media_sdk::EndOfFileEvent>(event.payload))
{
    m_dataBridge.finish(event.metadata.sessionId, event.metadata.generation);
    postControlEvent(event);
    return;
}
```

The exact helper names may differ, but EOF must enter data bridge before the Qt `endOfFile()` control signal is emitted.

- [ ] **Step 4: Verify focused checks**

Run:

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R "playplugin_bplus_completion_checks|playplugin_qt_playback_adapter_checks"
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add plugins/PlayPlugin/src/playback/QtPlaybackAdapter.h plugins/PlayPlugin/src/playback/QtPlaybackAdapter.cpp tests/playplugin_bplus_completion_checks.py plugins/PlayPlugin/PlayPluginExecutionFlow.md
git commit -m "[功能修复] 移除EOF临时重试路径"
```

---

### Task 6: Fix Stop/Open/Seek Cancellation Ordering

**Purpose:** Ensure blocking data bridge pushes can always be cancelled during stop, open, seek, and destruction.

**Files:**
- Modify: `plugins/PlayPlugin/src/playback/PlaybackPipeline.cpp`
- Modify: `plugins/PlayPlugin/src/playback/QtPlaybackAdapter.cpp`
- Modify: `plugins/PlayPlugin/src/playback/PlaybackDataBridge.h`
- Modify: `plugins/PlayPlugin/src/playback/PlaybackDataBridge.cpp`
- Modify: `tests/playplugin_bplus_completion_checks.py`

- [ ] **Step 1: Add shutdown-order checks**

Add text checks that enforce:
- `PlaybackPipeline::stopComponents()` aborts queues or cancels data bridge before waiting for audio renderer.
- `QtPlaybackAdapter::~QtPlaybackAdapter()` cancels bridge before resetting `media_sdk::Player`.
- `openFile()` starts from a fresh bridge session and clean queues.

- [ ] **Step 2: Define cancellation order**

Use this order for stop:

1. Cancel bridge / abort frame queues.
2. Stop SDK player.
3. Stop audio renderer and wait.
4. Stop/reset video renderer.
5. Flush queues.
6. Reset abort only after producers are stopped.

Use this order for open:

1. Stop old player/bridge.
2. Flush queues.
3. Reset abort.
4. Create/reset SDK player.
5. Start new session after `MediaInfoEvent`.

Use this order for seek:

1. Mark renderers seek-pending.
2. Cancel old generation data path.
3. Flush queues.
4. Submit SDK seek.
5. Resume accepting data only when `SeekCompletedEvent` arrives.

- [ ] **Step 3: Implement bridge cancellation**

`PlaybackDataBridge::cancel()` must:
- mark current session/generation invalid;
- abort both queues if necessary to wake blocked producers;
- return quickly and be safe to call multiple times.

If queue abort is global and would stop renderers, document reset order in `PlaybackPipeline.cpp` comments.

- [ ] **Step 4: Verify focused checks**

Run:

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R "playplugin_bplus_completion_checks|playplugin_qt_playback_adapter_checks"
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add plugins/PlayPlugin/src/playback/PlaybackPipeline.cpp plugins/PlayPlugin/src/playback/QtPlaybackAdapter.cpp plugins/PlayPlugin/src/playback/PlaybackDataBridge.h plugins/PlayPlugin/src/playback/PlaybackDataBridge.cpp tests/playplugin_bplus_completion_checks.py
git commit -m "[功能修复] 明确播放数据通道取消顺序"
```

---

### Task 7: Fix AudioRenderer Parameter Data Race

**Purpose:** Remove undefined behavior from cross-thread volume/mute state.

**Files:**
- Modify: `plugins/PlayPlugin/src/audio/AudioRenderer.h`
- Modify: `plugins/PlayPlugin/src/audio/AudioRenderer.cpp`
- Modify: `tests/playplugin_bplus_completion_checks.py`

- [ ] **Step 1: Add race-prevention source check**

Check that `m_paramDirty` is not read outside a `QMutexLocker` block, or replace it with an atomic flag with documented locking for `m_volume/m_muted`.

- [ ] **Step 2: Implement minimal lock-based fix**

Replace:

```cpp
if (m_paramDirty)
{
    QMutexLocker lk(&m_paramMutex);
    m_sink->setVolume(m_muted ? 0.0f : m_volume);
    m_paramDirty = false;
}
```

With:

```cpp
{
    QMutexLocker lk(&m_paramMutex);
    if (m_paramDirty)
    {
        m_sink->setVolume(m_muted ? 0.0f : m_volume);
        m_paramDirty = false;
    }
}
```

Keep the lock scope small and do not call any other renderer methods under this mutex.

- [ ] **Step 3: Verify**

Run:

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R "playplugin_bplus_completion_checks"
```

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add plugins/PlayPlugin/src/audio/AudioRenderer.h plugins/PlayPlugin/src/audio/AudioRenderer.cpp tests/playplugin_bplus_completion_checks.py
git commit -m "[功能修复] 修正音频参数跨线程访问"
```

---

### Task 8: Add Playback Diagnostics For Contract Verification

**Purpose:** Make early EOF, dropped frames, and queue waits observable instead of speculative.

**Files:**
- Modify: `plugins/PlayPlugin/src/playback/PlaybackDataBridge.h`
- Modify: `plugins/PlayPlugin/src/playback/PlaybackDataBridge.cpp`
- Modify: `plugins/PlayPlugin/src/video/VideoRenderer.cpp`
- Modify: `plugins/PlayPlugin/src/audio/AudioRenderer.cpp`

- [ ] **Step 1: Add bridge counters**

Add:

```cpp
struct PlaybackDataBridgeStats {
    std::uint64_t audioAccepted = 0;
    std::uint64_t videoAccepted = 0;
    std::uint64_t audioRejectedStale = 0;
    std::uint64_t videoRejectedStale = 0;
    std::uint64_t eofAccepted = 0;
    std::uint64_t queueAbortFailures = 0;
};
```

- [ ] **Step 2: Increment counters at decision points**

Rules:
- increment accepted only after queue push succeeds;
- increment stale only when session/generation mismatch rejects a frame;
- increment EOF accepted only after both applicable stream queues accept finish marker;
- increment abort failure when blocking push returns false because of cancellation.

- [ ] **Step 3: Log summary at EOF and stop**

Log one summary line per media session:

```text
PlayDataBridge: session=<id> generation=<n> audioAccepted=<n> videoAccepted=<n> staleAudio=<n> staleVideo=<n> eofAccepted=<n>
```

Do not log per frame.

- [ ] **Step 4: Verify**

Run:

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R "playplugin"
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add plugins/PlayPlugin/src/playback/PlaybackDataBridge.h plugins/PlayPlugin/src/playback/PlaybackDataBridge.cpp plugins/PlayPlugin/src/video/VideoRenderer.cpp plugins/PlayPlugin/src/audio/AudioRenderer.cpp
git commit -m "[功能修改] 增加播放数据通道诊断"
```

---

### Task 9: Remove Or Quarantine SDK Playback Scheduling From Core

**Purpose:** Align implementation with the corrected SDK boundary: core outputs frames; presenter decides timing.

**Files:**
- Modify: `sdk/media_core/src/DecodeWorker.h`
- Modify: `sdk/media_core/src/DecodeWorker.cpp`
- Modify: `sdk/media_core/src/ClockSync.h`
- Modify: `sdk/media_core/src/ClockSync.cpp`
- Modify: `sdk/media_core/src/FrameScheduler.h`
- Modify: `sdk/media_core/src/FrameScheduler.cpp`
- Modify: `sdk/media_core/tests/tst_media_core_primitives.cpp`
- Modify: `tests/media_sdk_core_architecture_checks.py`

- [ ] **Step 1: Add architecture check**

Check that `DecodeWorker` does not include or use `FrameScheduler`, and does not make render/wait/drop decisions.

Allowed in SDK core:
- timestamp normalization;
- frame metadata;
- decode diagnostics.

Not allowed in SDK core:
- video present scheduling;
- late video drop policy;
- audio-clock based render decisions.

- [ ] **Step 2: Remove unused core scheduling fields from `DecodeWorker`**

If `m_clock` is only used for position event convenience, replace it with local position tracking or remove it. Position events may be emitted from decoded audio PTS without maintaining a playback clock in core.

- [ ] **Step 3: Keep or move primitive tests intentionally**

If `FrameScheduler` remains as a pure utility for future presenter code, it must not be linked into `DecodeWorker` behavior. Prefer moving presenter scheduling tests to PlayPlugin tests if the utility is only used by `VideoRenderer`.

- [ ] **Step 4: Verify**

Run:

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_core"
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add sdk/media_core/src/DecodeWorker.h sdk/media_core/src/DecodeWorker.cpp sdk/media_core/src/ClockSync.h sdk/media_core/src/ClockSync.cpp sdk/media_core/src/FrameScheduler.h sdk/media_core/src/FrameScheduler.cpp sdk/media_core/tests/tst_media_core_primitives.cpp tests/media_sdk_core_architecture_checks.py
git commit -m "[功能修改] 收紧SDK播放调度边界"
```

---

### Task 10: Full Regression And Manual Playback Gate

**Purpose:** Prove the contract hardening did not introduce new playback regressions.

**Files:**
- Modify: `plugins/PlayPlugin/PlayPluginExecutionFlow.md`
- Modify: `docs/superpowers/specs/2026-06-22-media-sdk-bplus-design.md` only if implementation differs from the documented contract.

- [ ] **Step 1: Run full build**

Run:

```bash
cmake --build build --parallel
```

Expected: build succeeds.

- [ ] **Step 2: Run full CTest**

Run:

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 3: Manual playback checklist**

Run:

```bash
./build/app/PluginBasedApp
```

Verify manually:
- video appears within normal startup time;
- no green right-half frame corruption;
- audio is normal;
- pause then play does not freeze;
- playback does not end early;
- seek forward and backward does not show old frames;
- opening a second file does not display frames from the first file;
- EOF occurs only after audio/video drain.

- [ ] **Step 4: Update docs only for actual behavior**

If implementation names differ from the plan but contracts are equivalent, update:
- `plugins/PlayPlugin/PlayPluginExecutionFlow.md`
- `docs/superpowers/specs/2026-06-22-media-sdk-bplus-design.md`

Do not weaken the hard constraints to match a shortcut implementation.

- [ ] **Step 5: Commit final verification/docs**

```bash
git add plugins/PlayPlugin/PlayPluginExecutionFlow.md docs/superpowers/specs/2026-06-22-media-sdk-bplus-design.md
git commit -m "[文档] 同步播放链路加固结果"
```

If no docs changed, skip the commit and record verification in the final response.

---

## Self-Review

- Spec coverage: covers SDK boundary, event generation, true seek completion, EOF drain, backpressure, pause semantics, thread/lifecycle cancellation, observability, tests.
- Scope control: this plan intentionally does not add OpenGL presenter, new codec features, ABI stability, or third-party SDK packaging.
- Ordering: event metadata and seek completion come before data bridge; queue drain comes before EOF retry removal; cancellation comes before broader diagnostics.
- Risk control: every phase has focused tests and one commit; data-path changes are isolated from presenter scheduling; GUI blocking is explicitly forbidden.
- Data bridge threading decision: this plan uses the SDK callback thread for frame conversion and blocking queue push. Do not add a dedicated bridge worker in this hardening pass; add one only under a separate design if command responsiveness measurements prove it is necessary.
