# Media SDK B+ Frame Channel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split B+ playback control events from decoded audio/video frame delivery, and replace hidden `IEventSink::onEvent()` data backpressure with an explicit, cancellable, observable frame channel.

**Architecture:** Keep the current single `DecodeWorker` thread model for demux/decode. Make `IEventSink` control-only, add `IDecodeFrameSink` for decoded frames, make runtime frame enqueue return structured push results, then formalize cancellable backpressure inside runtime queues. PlayPlugin remains a Qt adapter/presentation layer.

**Tech Stack:** C++20 SDK core and playback runtime, C++17 Qt 6 PlayPlugin adapter, FFmpeg frame contracts, CMake/CTest, Python architecture checks.

---

## Execution Rules

- Execute one task at a time.
- Each task ends with one focused commit.
- Stop after each commit and ask the user to confirm before starting the next task.
- Do not split demux/audio decode/video decode into separate threads in this plan.
- Do not keep sending `AudioFrameEvent` or `VideoFrameEvent` through `IEventSink`.
- Do not use unbounded async queues to hide backpressure.
- Do not block the Qt GUI thread on frame push.
- Do not silently drop normal audio frames.
- Do not let PlayPlugin adapter make video drop policy decisions outside runtime/scheduler.
- Build and test the smallest relevant target before each commit; run full build/CTest at the final task.

## File Responsibility Map

### SDK Core

- Modify `sdk/media_core/include/media_sdk/Player.h`: `Player` constructor accepts `IEventSink&` and `IDecodeFrameSink&`; `IEventSink` is documented as control-only.
- Create `sdk/media_core/include/media_sdk/DecodeFrameSink.h`: frame channel metadata, status, result, and `IDecodeFrameSink`.
- Modify `sdk/media_core/include/media_sdk/MediaEvents.h`: stop treating audio/video frames as control events; remove payloads in the cleanup task if all callers have migrated.
- Modify `sdk/media_core/src/PlaybackController.h/.cpp`: store and forward `IDecodeFrameSink&`.
- Modify `sdk/media_core/src/DecodeWorker.h/.cpp`: store frame sink, call `pushAudio()` and `pushVideo()` instead of frame events, record push diagnostics.
- Modify `sdk/media_core/tests/tst_media_core_frame_contract.cpp`: interface and event payload contract checks.
- Modify `sdk/media_core/tests/tst_media_core_playback_worker.cpp`: worker emits control events through `IEventSink` and frames through `IDecodeFrameSink`.

### Playback Runtime

- Modify `sdk/media_playback_runtime/include/media_sdk/runtime/RuntimePlayer.h`: `enqueueAudio()` and `enqueueVideo()` return structured push results.
- Modify `sdk/media_playback_runtime/include/media_sdk/runtime/RuntimeTypes.h`: runtime push status/result and diagnostics fields.
- Modify `sdk/media_playback_runtime/src/RuntimeFrameQueue.h`: cancellable push, wait-time accounting, high watermark.
- Modify `sdk/media_playback_runtime/src/RuntimePlayer.cpp`: map queue results, cancel push waits on seek/stop/close.
- Modify `sdk/media_playback_runtime/tests/tst_runtime_frame_queue.cpp`: queue push result and cancellation tests.
- Modify `sdk/media_playback_runtime/tests/tst_runtime_player_mock.cpp`: runtime enqueue result and diagnostics tests.

### PlayPlugin

- Modify `plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.h/.cpp`: implement `IDecodeFrameSink`, perform synchronous runtime init on `MediaInfoEvent`, map runtime push results.
- Modify `plugins/PlayPlugin/src/playback/QtPlaybackAdapter.h/.cpp`: implement `IDecodeFrameSink` for the legacy Qt queue path and remove old frame event dependency.
- Modify `plugins/PlayPlugin/src/playback/PlaybackDataBridge.h/.cpp`: keep the legacy Qt queue bridge behavior; only make minimal signature or result-mapping changes required by `QtPlaybackAdapter`.

### Architecture Checks

- Modify `tests/media_sdk_core_architecture_checks.py`: `IEventSink` control-only and `DecodeWorker` must not emit frame events.
- Modify `tests/media_sdk_playback_runtime_architecture_checks.py`: runtime enqueue APIs must return push results and queue supports cancellation diagnostics.
- Modify `tests/playplugin_qt_playback_adapter_checks.py`: PlayPlugin adapter must not rely on `AudioFrameEvent` / `VideoFrameEvent` data path.

## Verification Commands

Use focused commands during tasks:

```bash
cmake --build build --target MediaSdkCoreFrameContractTest MediaSdkCorePlaybackWorkerTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_core_frame_contract|media_sdk_core_playback_worker|media_sdk_core_architecture_checks"
cmake --build build --target MediaSdkPlaybackRuntimeFrameQueueTest MediaSdkPlaybackRuntimePlayerMockTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_playback_runtime_frame_queue|media_sdk_playback_runtime_player_mock|media_sdk_playback_runtime_architecture_checks"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

If `build/` is missing, configure using the repository command from `AGENTS.md`.

---

## Task 1: Add Frame Channel Interface And Control-Only Contract

**Purpose:** Introduce the public SDK frame channel contract before changing decode behavior.

**Files:**
- Create: `sdk/media_core/include/media_sdk/DecodeFrameSink.h`
- Modify: `sdk/media_core/include/media_sdk/Player.h`
- Modify: `sdk/media_core/tests/tst_media_core_frame_contract.cpp`
- Modify: `tests/media_sdk_core_architecture_checks.py`

- [ ] **Step 1: Write failing frame sink contract tests**

Add compile/value checks to `sdk/media_core/tests/tst_media_core_frame_contract.cpp`:

```cpp
#include "media_sdk/DecodeFrameSink.h"

class RecordingFrameSink final : public media_sdk::IDecodeFrameSink {
public:
    media_sdk::DecodeFramePushResult pushAudio(
        media_sdk::AudioFrame frame,
        media_sdk::DecodeFrameMetadata metadata) override
    {
        ++audioCount;
        lastSessionId = metadata.sessionId;
        lastGeneration = metadata.generation;
        lastAudioPts = frame.pts();
        return { .status = media_sdk::DecodeFramePushStatus::Accepted };
    }

    media_sdk::DecodeFramePushResult pushVideo(
        media_sdk::VideoFrame frame,
        media_sdk::DecodeFrameMetadata metadata) override
    {
        ++videoCount;
        lastSessionId = metadata.sessionId;
        lastGeneration = metadata.generation;
        lastVideoPts = frame.pts();
        return { .status = media_sdk::DecodeFramePushStatus::Accepted };
    }

    int audioCount = 0;
    int videoCount = 0;
    std::uint64_t lastSessionId = 0;
    std::uint64_t lastGeneration = 0;
    std::chrono::microseconds lastAudioPts { 0 };
    std::chrono::microseconds lastVideoPts { 0 };
};

void testDecodeFrameSinkContract()
{
    RecordingFrameSink sink;
    auto audio = media_sdk::AudioFrame::fromOwnedSamples(
        media_sdk::AudioSampleFormat::Float32Interleaved,
        48000,
        2,
        std::chrono::microseconds { 1000 },
        {});
    auto result = sink.pushAudio(std::move(audio), { .sessionId = 7, .generation = 3 });
    assert(result.status == media_sdk::DecodeFramePushStatus::Accepted);
    assert(sink.audioCount == 1);
    assert(sink.lastSessionId == 7);
    assert(sink.lastGeneration == 3);
}
```

Call `testDecodeFrameSinkContract()` from `main()`.

- [ ] **Step 2: Add failing architecture checks**

In `tests/media_sdk_core_architecture_checks.py`, add checks:

```python
decode_frame_sink = SDK_ROOT / "include" / "media_sdk" / "DecodeFrameSink.h"
assert_contains(read(decode_frame_sink), "class IDecodeFrameSink", decode_frame_sink)
assert_contains(read(decode_frame_sink), "DecodeFramePushStatus", decode_frame_sink)
assert_contains(read(decode_frame_sink), "DecodeFramePushResult", decode_frame_sink)

player_header = SDK_ROOT / "include" / "media_sdk" / "Player.h"
player_text = read(player_header)
assert_contains(player_text, "control", player_header)
```

- [ ] **Step 3: Run focused tests and verify failure**

Run:

```bash
cmake --build build --target MediaSdkCoreFrameContractTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_core_frame_contract|media_sdk_core_architecture_checks"
```

Expected: FAIL because `DecodeFrameSink.h` and `IDecodeFrameSink` do not exist yet.

- [ ] **Step 4: Add `DecodeFrameSink.h`**

Create `sdk/media_core/include/media_sdk/DecodeFrameSink.h`:

```cpp
#pragma once

#include "media_sdk/Frame.h"

#include <chrono>
#include <cstdint>

namespace media_sdk {

struct DecodeFrameMetadata {
    std::uint64_t sessionId = 0;
    std::uint64_t generation = 0;
};

enum class DecodeFramePushStatus {
    Accepted,
    Backpressured,
    StaleGeneration,
    Cancelled,
    Closed
};

struct DecodeFramePushResult {
    DecodeFramePushStatus status = DecodeFramePushStatus::Closed;
    std::chrono::microseconds waitTime { 0 };
};

class IDecodeFrameSink {
public:
    virtual ~IDecodeFrameSink() = default;

    [[nodiscard("frame push result determines whether decode can continue, retry, or stop")]]
    virtual DecodeFramePushResult pushAudio(AudioFrame frame, DecodeFrameMetadata metadata) = 0;

    [[nodiscard("frame push result determines whether decode can continue, retry, or stop")]]
    virtual DecodeFramePushResult pushVideo(VideoFrame frame, DecodeFrameMetadata metadata) = 0;
};

} // namespace media_sdk
```

- [ ] **Step 5: Document `IEventSink` as control-only**

Modify `sdk/media_core/include/media_sdk/Player.h`:

```cpp
class IEventSink
{
public:
    virtual ~IEventSink() = default;
    // Control events only. Decoded audio/video frames are delivered through IDecodeFrameSink.
    virtual void onEvent(const PlayerEvent& event) = 0;
};
```

- [ ] **Step 6: Verify Task 1**

Run:

```bash
cmake --build build --target MediaSdkCoreFrameContractTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_core_frame_contract|media_sdk_core_architecture_checks"
```

Expected: PASS. Task 1 only adds the frame sink interface and documents `IEventSink`; constructor wiring happens in Task 2.

- [ ] **Step 7: Commit Task 1**

```bash
git add sdk/media_core/include/media_sdk/DecodeFrameSink.h sdk/media_core/include/media_sdk/Player.h sdk/media_core/tests/tst_media_core_frame_contract.cpp tests/media_sdk_core_architecture_checks.py
git commit -m "[架构修改] 增加解码帧通道接口"
```

Stop and ask the user to confirm Task 2.

---

## Task 2: Thread Frame Sink Through Player And DecodeWorker

**Purpose:** Make `Player`, `PlaybackController`, and `DecodeWorker` own explicit non-owning references to both control event sink and frame sink.

**Files:**
- Modify: `sdk/media_core/include/media_sdk/Player.h`
- Modify: `sdk/media_core/src/Player.cpp`
- Modify: `sdk/media_core/src/PlaybackController.h`
- Modify: `sdk/media_core/src/PlaybackController.cpp`
- Modify: `sdk/media_core/src/DecodeWorker.h`
- Modify: `sdk/media_core/src/DecodeWorker.cpp`
- Modify: `sdk/media_core/tests/tst_media_core_playback_worker.cpp`
- Modify: `plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.h/.cpp`
- Modify: `plugins/PlayPlugin/src/playback/QtPlaybackAdapter.h/.cpp`

- [ ] **Step 1: Add test frame sink to playback worker tests**

In `sdk/media_core/tests/tst_media_core_playback_worker.cpp`, add a sink:

```cpp
class RecordingFrameSink final : public media_sdk::IDecodeFrameSink {
public:
    media_sdk::DecodeFramePushResult pushAudio(
        media_sdk::AudioFrame frame,
        media_sdk::DecodeFrameMetadata metadata) override
    {
        std::lock_guard lock(mutex);
        ++audioFrames;
        lastSessionId = metadata.sessionId;
        lastGeneration = metadata.generation;
        lastAudioPts = frame.pts();
        cv.notify_all();
        return { .status = media_sdk::DecodeFramePushStatus::Accepted };
    }

    media_sdk::DecodeFramePushResult pushVideo(
        media_sdk::VideoFrame frame,
        media_sdk::DecodeFrameMetadata metadata) override
    {
        std::lock_guard lock(mutex);
        ++videoFrames;
        lastSessionId = metadata.sessionId;
        lastGeneration = metadata.generation;
        lastVideoPts = frame.pts();
        cv.notify_all();
        return { .status = media_sdk::DecodeFramePushStatus::Accepted };
    }

    std::mutex mutex;
    std::condition_variable cv;
    int audioFrames = 0;
    int videoFrames = 0;
    std::uint64_t lastSessionId = 0;
    std::uint64_t lastGeneration = 0;
    std::chrono::microseconds lastAudioPts { 0 };
    std::chrono::microseconds lastVideoPts { 0 };
};
```

Update test player construction:

```cpp
RecordingSink events;
RecordingFrameSink frames;
media_sdk::Player player(media_sdk::PlayerConfig {}, events, frames);
```

- [ ] **Step 2: Run focused build and verify failure**

Run:

```bash
cmake --build build --target MediaSdkCorePlaybackWorkerTest --parallel
```

Expected: FAIL because `Player`, `PlaybackController`, and `DecodeWorker` constructors are not updated consistently.

- [ ] **Step 3: Update `Player.h` constructor declaration**

Modify `sdk/media_core/include/media_sdk/Player.h`:

```cpp
#include "media_sdk/DecodeFrameSink.h"
```

Change the constructor declaration:

```cpp
explicit Player(PlayerConfig config, IEventSink& events, IDecodeFrameSink& frames);
```

- [ ] **Step 4: Update `Player.cpp`**

Modify the implementation shape:

```cpp
class Player::Impl {
public:
    Impl(PlayerConfig config, IEventSink& events, IDecodeFrameSink& frames)
        : controller(std::move(config), events, frames)
    {
    }

    PlaybackController controller;
};

Player::Player(PlayerConfig config, IEventSink& events, IDecodeFrameSink& frames)
    : m_impl(std::make_unique<Impl>(std::move(config), events, frames))
{
}
```

- [ ] **Step 5: Update `PlaybackController`**

Modify `sdk/media_core/src/PlaybackController.h`:

```cpp
#include "media_sdk/DecodeFrameSink.h"

class PlaybackController
{
public:
    PlaybackController(PlayerConfig config, IEventSink& events, IDecodeFrameSink& frames);
```

Modify `sdk/media_core/src/PlaybackController.cpp`:

```cpp
PlaybackController::PlaybackController(PlayerConfig config,
                                       IEventSink& events,
                                       IDecodeFrameSink& frames)
    : m_worker(std::move(config), events, frames)
{
}
```

- [ ] **Step 6: Update `DecodeWorker` constructor and member**

Modify `sdk/media_core/src/DecodeWorker.h`:

```cpp
#include "media_sdk/DecodeFrameSink.h"

DecodeWorker(PlayerConfig config, IEventSink& events, IDecodeFrameSink& frames);

IEventSink& m_events;
IDecodeFrameSink& m_frames;
```

Modify `sdk/media_core/src/DecodeWorker.cpp` constructor:

```cpp
DecodeWorker::DecodeWorker(PlayerConfig config, IEventSink& events, IDecodeFrameSink& frames)
    : m_config(std::move(config))
    , m_events(events)
    , m_frames(frames)
{
    m_thread = std::make_unique<WorkerThread>([this](WorkerStopToken stopToken) {
        run(stopToken);
    });
}
```

- [ ] **Step 7: Update PlayPlugin adapters as temporary frame sinks**

In `SdkPlaybackAdapter.h` and `QtPlaybackAdapter.h`, inherit from `media_sdk::IDecodeFrameSink` and declare:

```cpp
media_sdk::DecodeFramePushResult pushAudio(
    media_sdk::AudioFrame frame,
    media_sdk::DecodeFrameMetadata metadata) override;
media_sdk::DecodeFramePushResult pushVideo(
    media_sdk::VideoFrame frame,
    media_sdk::DecodeFrameMetadata metadata) override;
```

Update `Player` construction to pass `*this` as both sinks:

```cpp
m_player = std::make_unique<media_sdk::Player>(config, *this, *this);
```

The method bodies can initially return `Closed`; Task 3 wires actual behavior:

```cpp
return { .status = media_sdk::DecodeFramePushStatus::Closed };
```

- [ ] **Step 8: Verify Task 2**

Run:

```bash
cmake --build build --target MediaSdkCorePlaybackWorkerTest PlayPlugin --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_core_playback_worker"
```

Expected: PASS or existing runtime behavior unchanged until Task 3 routes frames.

- [ ] **Step 9: Commit Task 2**

```bash
git add sdk/media_core/include/media_sdk/Player.h sdk/media_core/src/Player.cpp sdk/media_core/src/PlaybackController.h sdk/media_core/src/PlaybackController.cpp sdk/media_core/src/DecodeWorker.h sdk/media_core/src/DecodeWorker.cpp sdk/media_core/tests/tst_media_core_playback_worker.cpp plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.h plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp plugins/PlayPlugin/src/playback/QtPlaybackAdapter.h plugins/PlayPlugin/src/playback/QtPlaybackAdapter.cpp
git commit -m "[架构修改] 贯通解码帧通道依赖"
```

Stop and ask the user to confirm Task 3.

---

## Task 3: Move Decoded Frames Off IEventSink

**Purpose:** Stop sending decoded audio/video frames as `PlayerEvent` payloads and route them through `IDecodeFrameSink`.

**Files:**
- Modify: `sdk/media_core/src/DecodeWorker.h`
- Modify: `sdk/media_core/src/DecodeWorker.cpp`
- Modify: `sdk/media_core/tests/tst_media_core_playback_worker.cpp`
- Modify: `tests/media_sdk_core_architecture_checks.py`

- [ ] **Step 1: Add failing event separation assertions**

In the playback worker test `RecordingSink`, count frame events and assert they remain zero:

```cpp
int audioFrameEvents = 0;
int videoFrameEvents = 0;

void onEvent(const media_sdk::PlayerEvent& event) override
{
    if (std::holds_alternative<media_sdk::AudioFrameEvent>(event.payload))
        ++audioFrameEvents;
    if (std::holds_alternative<media_sdk::VideoFrameEvent>(event.payload))
        ++videoFrameEvents;
    events.push_back(event);
}
```

After playback receives frames through `RecordingFrameSink`, assert:

```cpp
assert(events.audioFrameEvents == 0);
assert(events.videoFrameEvents == 0);
assert(frames.audioFrames > 0 || frames.videoFrames > 0);
```

- [ ] **Step 2: Add architecture check**

In `tests/media_sdk_core_architecture_checks.py`, add:

```python
decode_worker = SDK_ROOT / "src" / "DecodeWorker.cpp"
decode_worker_text = read(decode_worker)
for forbidden in ("makeEvent(AudioFrameEvent", "makeEvent(VideoFrameEvent"):
    require(forbidden not in decode_worker_text,
            f"DecodeWorker must not emit decoded frames through IEventSink: {forbidden}")
```

- [ ] **Step 3: Run focused tests and verify failure**

Run:

```bash
cmake --build build --target MediaSdkCorePlaybackWorkerTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_core_playback_worker|media_sdk_core_architecture_checks"
```

Expected: FAIL because `DecodeWorker` still emits `AudioFrameEvent` and `VideoFrameEvent`.

- [ ] **Step 4: Add helper metadata and frame push handling**

In `DecodeWorker.cpp`, add:

```cpp
DecodeFrameMetadata DecodeWorker::frameMetadata() const
{
    return { .sessionId = m_sessionId, .generation = m_generation };
}

bool DecodeWorker::handleFramePushResult(DecodeFramePushResult result)
{
    switch (result.status) {
    case DecodeFramePushStatus::Accepted:
    case DecodeFramePushStatus::Backpressured:
    case DecodeFramePushStatus::StaleGeneration:
        return true;
    case DecodeFramePushStatus::Cancelled:
    case DecodeFramePushStatus::Closed:
        return false;
    }
    return false;
}
```

Declare both helpers in `DecodeWorker.h`.

- [ ] **Step 5: Route audio frames through `pushAudio()`**

Replace audio frame emission:

```cpp
AudioFrame audioFrame = makeAudioFrame(std::move(frame));
emitEvent(makeEvent(PositionChangedEvent {
    std::chrono::duration_cast<std::chrono::milliseconds>(audioFrame.pts()) }));
const auto pushResult = m_frames.pushAudio(std::move(audioFrame), frameMetadata());
return handleFramePushResult(pushResult);
```

- [ ] **Step 6: Route video frames through `pushVideo()`**

Replace `emitVideoFrame()` body:

```cpp
bool DecodeWorker::emitVideoFrame(VideoFrame frame)
{
    const auto pushResult = m_frames.pushVideo(std::move(frame), frameMetadata());
    return handleFramePushResult(pushResult);
}
```

- [ ] **Step 7: Route flush/drain frames through frame sink**

Ensure flush paths use the same `pushAudio()` / `pushVideo()` helpers:

```cpp
AudioFrame audioFrame = makeAudioFrame(std::move(frame));
return handleFramePushResult(m_frames.pushAudio(std::move(audioFrame), frameMetadata()));
```

- [ ] **Step 8: Verify Task 3**

Run:

```bash
cmake --build build --target MediaSdkCorePlaybackWorkerTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_core_playback_worker|media_sdk_core_architecture_checks"
```

Expected: PASS. Worker tests observe frames through `IDecodeFrameSink`, not `IEventSink`.

- [ ] **Step 9: Commit Task 3**

```bash
git add sdk/media_core/src/DecodeWorker.h sdk/media_core/src/DecodeWorker.cpp sdk/media_core/tests/tst_media_core_playback_worker.cpp tests/media_sdk_core_architecture_checks.py
git commit -m "[架构修改] 将解码帧迁出控制事件通道"
```

Stop and ask the user to confirm Task 4.

---

## Task 4: Make Runtime Frame Enqueue Return Structured Results

**Purpose:** Let PlayPlugin map runtime queue behavior back to `DecodeFramePushResult` without guessing or ignoring backpressure.

**Files:**
- Modify: `sdk/media_playback_runtime/include/media_sdk/runtime/RuntimeTypes.h`
- Modify: `sdk/media_playback_runtime/include/media_sdk/runtime/RuntimePlayer.h`
- Modify: `sdk/media_playback_runtime/src/RuntimePlayer.cpp`
- Modify: `sdk/media_playback_runtime/tests/tst_runtime_player_mock.cpp`
- Modify: `tests/media_sdk_playback_runtime_architecture_checks.py`

- [ ] **Step 1: Add failing runtime result tests**

In `tst_runtime_player_mock.cpp`, update calls:

```cpp
const auto audioResult = player.enqueueAudio(runtimeAudio(1, 1, 42ms));
assert(audioResult.status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);

const auto videoResult = player.enqueueVideo(runtimeVideo(1, 1, 101ms));
assert(videoResult.status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);
```

Add stale generation assertions:

```cpp
const auto staleResult = player.enqueueVideo(runtimeVideo(1, 99, 101ms));
assert(staleResult.status == media_sdk::runtime::RuntimeFramePushStatus::RejectedGeneration);
```

- [ ] **Step 2: Add architecture check**

In `tests/media_sdk_playback_runtime_architecture_checks.py`, require:

```python
runtime_player_h = RUNTIME / "include" / "media_sdk" / "runtime" / "RuntimePlayer.h"
runtime_player_text = read(runtime_player_h)
assert_contains(runtime_player_text, "RuntimeFramePushResult enqueueAudio", runtime_player_h)
assert_contains(runtime_player_text, "RuntimeFramePushResult enqueueVideo", runtime_player_h)
```

- [ ] **Step 3: Run focused tests and verify failure**

Run:

```bash
cmake --build build --target MediaSdkPlaybackRuntimePlayerMockTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_playback_runtime_player_mock|media_sdk_playback_runtime_architecture_checks"
```

Expected: FAIL because enqueue APIs return `void`.

- [ ] **Step 4: Add runtime push result types**

In `RuntimeTypes.h`:

```cpp
enum class RuntimeFramePushStatus {
    Accepted,
    Backpressured,
    RejectedGeneration,
    Cancelled,
    Closed
};

struct RuntimeFramePushResult {
    RuntimeFramePushStatus status = RuntimeFramePushStatus::Closed;
    std::chrono::microseconds waitTime { 0 };
};
```

- [ ] **Step 5: Change public runtime enqueue signatures**

In `RuntimePlayer.h`:

```cpp
[[nodiscard("frame enqueue result tells the producer whether runtime accepted, rejected, or cancelled the frame")]]
RuntimeFramePushResult enqueueAudio(RuntimeAudioFrame frame);

[[nodiscard("frame enqueue result tells the producer whether runtime accepted, rejected, or cancelled the frame")]]
RuntimeFramePushResult enqueueVideo(RuntimeVideoFrame frame);
```

- [ ] **Step 6: Map existing queue push results**

In `RuntimePlayer.cpp`, change implementation:

```cpp
RuntimeFramePushResult enqueueAudio(RuntimeAudioFrame frame)
{
    if (!isRunning())
        return { .status = RuntimeFramePushStatus::Closed };

    const auto pushResult = audioQueue.push(std::move(frame));
    if (pushResult == RuntimeFrameQueue<RuntimeAudioFrame>::PushResult::Accepted) {
        std::lock_guard lock(m_mutex);
        ++diagnostics.audioQueued;
        return { .status = RuntimeFramePushStatus::Accepted };
    }
    if (pushResult == RuntimeFrameQueue<RuntimeAudioFrame>::PushResult::RejectedGeneration)
        return { .status = RuntimeFramePushStatus::RejectedGeneration };
    if (pushResult == RuntimeFrameQueue<RuntimeAudioFrame>::PushResult::Aborted)
        return { .status = RuntimeFramePushStatus::Cancelled };
    return { .status = RuntimeFramePushStatus::Closed };
}
```

Apply the same mapping to `enqueueVideo()`, preserving native accepted diagnostics when accepted.

- [ ] **Step 7: Verify Task 4**

Run:

```bash
cmake --build build --target MediaSdkPlaybackRuntimePlayerMockTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_playback_runtime_player_mock|media_sdk_playback_runtime_architecture_checks"
```

Expected: PASS.

- [ ] **Step 8: Commit Task 4**

```bash
git add sdk/media_playback_runtime/include/media_sdk/runtime/RuntimeTypes.h sdk/media_playback_runtime/include/media_sdk/runtime/RuntimePlayer.h sdk/media_playback_runtime/src/RuntimePlayer.cpp sdk/media_playback_runtime/tests/tst_runtime_player_mock.cpp tests/media_sdk_playback_runtime_architecture_checks.py
git commit -m "[功能修改] 返回runtime帧入队结果"
```

Stop and ask the user to confirm Task 5.

---

## Task 5: Wire PlayPlugin Frame Sink To Runtime

**Purpose:** Make `SdkPlaybackAdapter` the bridge from `IDecodeFrameSink` to `RuntimePlayer`, with synchronous runtime initialization on `MediaInfoEvent`.

**Files:**
- Modify: `plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.h`
- Modify: `plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp`
- Modify: `tests/playplugin_qt_playback_adapter_checks.py`

- [ ] **Step 1: Add architecture checks for adapter frame sink**

In `tests/playplugin_qt_playback_adapter_checks.py`, require:

```python
sdk_adapter_h = read("plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.h")
require("public media_sdk::IDecodeFrameSink" in sdk_adapter_h,
        "SdkPlaybackAdapter must implement IDecodeFrameSink")
require("pushAudio(" in sdk_adapter_h and "pushVideo(" in sdk_adapter_h,
        "SdkPlaybackAdapter must expose frame sink methods")

sdk_adapter_cpp = read("plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp")
require("ensureRuntimeForMedia" in sdk_adapter_cpp,
        "MediaInfoEvent must synchronously ensure runtime before queued UI handling")
require("RuntimeFramePushStatus" in sdk_adapter_cpp,
        "SdkPlaybackAdapter must map runtime frame push results")
```

- [ ] **Step 2: Run check and verify failure**

Run:

```bash
python3 tests/playplugin_qt_playback_adapter_checks.py
```

Expected: FAIL until adapter implements the new interface.

- [ ] **Step 3: Include frame sink and declare methods**

In `SdkPlaybackAdapter.h`:

```cpp
#include "media_sdk/DecodeFrameSink.h"

class SdkPlaybackAdapter final : public QObject
    , public media_sdk::IEventSink
    , public media_sdk::IDecodeFrameSink
    , public media_sdk::runtime::IRuntimePlayerEvents
{
    media_sdk::DecodeFramePushResult pushAudio(
        media_sdk::AudioFrame frame,
        media_sdk::DecodeFrameMetadata metadata) override;
    media_sdk::DecodeFramePushResult pushVideo(
        media_sdk::VideoFrame frame,
        media_sdk::DecodeFrameMetadata metadata) override;
};
```

- [ ] **Step 4: Make MediaInfo initialize runtime synchronously**

In `SdkPlaybackAdapter::onEvent()`:

```cpp
if (const auto* mediaInfo = std::get_if<media_sdk::MediaInfoEvent>(&event.payload)) {
    if (!m_pendingFallback && !ensureRuntimeForMedia(mediaInfo->info, m_directNativeVideoEnabled))
        return;
    if (!m_pendingFallback)
        setAcceptedCoreTimeline(event.metadata, currentTimeline());
}
```

Keep UI handling queued:

```cpp
auto eventCopy = std::make_shared<media_sdk::PlayerEvent>(event);
QMetaObject::invokeMethod(this,
                          [this, eventCopy, seekCompletion]() {
                              handleControlEvent(*eventCopy, seekCompletion);
                          },
                          Qt::QueuedConnection);
```

- [ ] **Step 5: Implement result mapping**

Add helper in `SdkPlaybackAdapter.cpp`:

```cpp
media_sdk::DecodeFramePushResult mapRuntimePushResult(
    media_sdk::runtime::RuntimeFramePushResult result)
{
    using RuntimeStatus = media_sdk::runtime::RuntimeFramePushStatus;
    using DecodeStatus = media_sdk::DecodeFramePushStatus;

    switch (result.status) {
    case RuntimeStatus::Accepted:
        return { .status = DecodeStatus::Accepted, .waitTime = result.waitTime };
    case RuntimeStatus::Backpressured:
        return { .status = DecodeStatus::Backpressured, .waitTime = result.waitTime };
    case RuntimeStatus::RejectedGeneration:
        return { .status = DecodeStatus::StaleGeneration, .waitTime = result.waitTime };
    case RuntimeStatus::Cancelled:
        return { .status = DecodeStatus::Cancelled, .waitTime = result.waitTime };
    case RuntimeStatus::Closed:
        return { .status = DecodeStatus::Closed, .waitTime = result.waitTime };
    }
    return { .status = DecodeStatus::Closed, .waitTime = result.waitTime };
}
```

- [ ] **Step 6: Implement `pushAudio()` and `pushVideo()`**

In `SdkPlaybackAdapter.cpp`:

```cpp
media_sdk::DecodeFramePushResult SdkPlaybackAdapter::pushAudio(
    media_sdk::AudioFrame frame,
    media_sdk::DecodeFrameMetadata metadata)
{
    std::shared_ptr<media_sdk::runtime::RuntimePlayer> runtimePlayer;
    media_sdk::runtime::RuntimeTimeline runtimeTimeline;
    {
        std::lock_guard lock(m_mutex);
        if (!m_acceptingRuntimeFrames || !m_runtimePlayer || !acceptsCoreEvent({
                .sessionId = metadata.sessionId,
                .generation = metadata.generation,
            })) {
            return { .status = media_sdk::DecodeFramePushStatus::StaleGeneration };
        }
        runtimePlayer = m_runtimePlayer;
        runtimeTimeline = m_runtimeTimeline;
    }

    return mapRuntimePushResult(runtimePlayer->enqueueAudio(runtimeAudioFrame(frame, runtimeTimeline)));
}

media_sdk::DecodeFramePushResult SdkPlaybackAdapter::pushVideo(
    media_sdk::VideoFrame frame,
    media_sdk::DecodeFrameMetadata metadata)
{
    std::shared_ptr<media_sdk::runtime::RuntimePlayer> runtimePlayer;
    media_sdk::runtime::RuntimeTimeline runtimeTimeline;
    {
        std::lock_guard lock(m_mutex);
        if (!m_acceptingRuntimeFrames || !m_runtimePlayer || !acceptsCoreEvent({
                .sessionId = metadata.sessionId,
                .generation = metadata.generation,
            })) {
            return { .status = media_sdk::DecodeFramePushStatus::StaleGeneration };
        }
        runtimePlayer = m_runtimePlayer;
        runtimeTimeline = m_runtimeTimeline;
    }

    return mapRuntimePushResult(runtimePlayer->enqueueVideo(runtimeVideoFrame(std::move(frame), runtimeTimeline)));
}
```

- [ ] **Step 7: Remove old frame handling from `handleDataEvent()`**

Delete branches handling `AudioFrameEvent` and `VideoFrameEvent`. `handleDataEvent()` should no longer be responsible for decoded frame delivery.

- [ ] **Step 8: Verify Task 5**

Run:

```bash
cmake --build build --target PlayPlugin MediaSdkCorePlaybackWorkerTest MediaSdkPlaybackRuntimePlayerMockTest --parallel
ctest --test-dir build --output-on-failure -R "playplugin_qt_playback_adapter_checks|media_sdk_core_playback_worker|media_sdk_playback_runtime_player_mock"
```

Expected: PASS.

- [ ] **Step 9: Commit Task 5**

```bash
git add plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.h plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp tests/playplugin_qt_playback_adapter_checks.py
git commit -m "[架构修改] 用帧通道连接SDK和runtime"
```

Stop and ask the user to confirm Task 6.

---

## Task 6: Add Cancellable Runtime Queue Backpressure

**Purpose:** Replace indefinite hidden queue waits with explicit push results, cancellation, wait-time diagnostics, and high-watermark tracking.

**Files:**
- Modify: `sdk/media_playback_runtime/src/RuntimeFrameQueue.h`
- Modify: `sdk/media_playback_runtime/include/media_sdk/runtime/RuntimeTypes.h`
- Modify: `sdk/media_playback_runtime/src/RuntimePlayer.cpp`
- Modify: `sdk/media_playback_runtime/tests/tst_runtime_frame_queue.cpp`
- Modify: `sdk/media_playback_runtime/tests/tst_runtime_player_mock.cpp`

- [ ] **Step 1: Add failing cancellation and wait-time tests**

In `tst_runtime_frame_queue.cpp`, add:

```cpp
void pushWaitIsCancelledByAbort()
{
    media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame> queue(1);
    queue.reset(10, 2);
    assert(queue.push(makeFrame(10, 2, 100)).status
        == media_sdk::runtime::RuntimeFramePushStatus::Accepted);

    auto future = std::async(std::launch::async, [&queue]() {
        return queue.push(makeFrame(10, 2, 200));
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    queue.abort();
    const auto result = future.get();
    assert(result.status == media_sdk::runtime::RuntimeFramePushStatus::Cancelled);
}

void pushReportsBackpressureWhenItWaited()
{
    media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame> queue(1);
    queue.reset(10, 2);
    assert(queue.push(makeFrame(10, 2, 100)).status
        == media_sdk::runtime::RuntimeFramePushStatus::Accepted);

    auto future = std::async(std::launch::async, [&queue]() {
        return queue.push(makeFrame(10, 2, 200));
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    media_sdk::runtime::RuntimeVideoFrame popped;
    assert(queue.waitPop(popped) == media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame>::PopResult::Frame);

    const auto result = future.get();
    assert(result.status == media_sdk::runtime::RuntimeFramePushStatus::Backpressured);
    assert(result.waitTime > std::chrono::microseconds { 0 });
    assert(queue.highWatermark() == 1);
}
```

- [ ] **Step 2: Run focused queue tests and verify failure**

Run:

```bash
cmake --build build --target MediaSdkPlaybackRuntimeFrameQueueTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_playback_runtime_frame_queue"
```

Expected: FAIL because `RuntimeFrameQueue::push()` does not return `RuntimeFramePushResult` and lacks high watermark.

- [ ] **Step 3: Change `RuntimeFrameQueue::push()` return type**

In `RuntimeFrameQueue.h`, replace `PushResult` return with `RuntimeFramePushResult`:

```cpp
[[nodiscard("push result distinguishes accepted, backpressured, rejected, cancelled, and closed")]]
RuntimeFramePushResult push(FrameType frame)
{
    return pushEntry(Entry { std::move(frame), false });
}
```

Keep `PopResult` unchanged.

- [ ] **Step 4: Implement timed wait accounting**

In `pushEntry()`:

```cpp
const auto waitStart = std::chrono::steady_clock::now();
bool waited = false;
m_notFull.wait(lock, [this, &waited]()
{
    waited = true;
    return m_aborted || m_closed || m_queue.size() < m_capacity;
});
const auto waitTime = waited
    ? std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - waitStart)
    : std::chrono::microseconds { 0 };
```

Map states:

```cpp
if (m_aborted)
    return { .status = RuntimeFramePushStatus::Cancelled, .waitTime = waitTime };
if (m_closed)
    return { .status = RuntimeFramePushStatus::Closed, .waitTime = waitTime };
```

After push:

```cpp
m_highWatermark = std::max(m_highWatermark, m_queue.size());
return {
    .status = waited && waitTime > std::chrono::microseconds { 0 }
        ? RuntimeFramePushStatus::Backpressured
        : RuntimeFramePushStatus::Accepted,
    .waitTime = waitTime,
};
```

- [ ] **Step 5: Add `highWatermark()`**

In `RuntimeFrameQueue.h`:

```cpp
[[nodiscard]]
std::size_t highWatermark() const
{
    std::scoped_lock lock(m_mutex);
    return m_highWatermark;
}
```

Reset it in `reset()`:

```cpp
m_highWatermark = 0;
```

- [ ] **Step 6: Update `RuntimePlayer` mappings and diagnostics**

When result is `Backpressured`, increment queue and wait diagnostics:

```cpp
if (pushResult.status == RuntimeFramePushStatus::Accepted ||
    pushResult.status == RuntimeFramePushStatus::Backpressured) {
    std::lock_guard lock(m_mutex);
    ++diagnostics.audioQueued;
    if (pushResult.status == RuntimeFramePushStatus::Backpressured) {
        ++diagnostics.audioBackpressureCount;
        diagnostics.decodeFramePushWaitUs += pushResult.waitTime.count();
    }
    diagnostics.audioQueueHighWatermark = std::max(
        diagnostics.audioQueueHighWatermark,
        audioQueue.highWatermark());
    return pushResult;
}
```

Apply corresponding video diagnostics.

- [ ] **Step 7: Verify Task 6**

Run:

```bash
cmake --build build --target MediaSdkPlaybackRuntimeFrameQueueTest MediaSdkPlaybackRuntimePlayerMockTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_playback_runtime_frame_queue|media_sdk_playback_runtime_player_mock"
```

Expected: PASS.

- [ ] **Step 8: Commit Task 6**

```bash
git add sdk/media_playback_runtime/src/RuntimeFrameQueue.h sdk/media_playback_runtime/include/media_sdk/runtime/RuntimeTypes.h sdk/media_playback_runtime/src/RuntimePlayer.cpp sdk/media_playback_runtime/tests/tst_runtime_frame_queue.cpp sdk/media_playback_runtime/tests/tst_runtime_player_mock.cpp
git commit -m "[质量优化] 正式化runtime帧队列背压"
```

Stop and ask the user to confirm Task 7.

---

## Task 7: Remove Frame Event Payload Compatibility

**Purpose:** Delete the obsolete event payload route so future code cannot accidentally reintroduce frame delivery through `IEventSink`.

**Files:**
- Modify: `sdk/media_core/include/media_sdk/MediaEvents.h`
- Modify: `sdk/media_core/tests/tst_media_core_frame_contract.cpp`
- Modify: `plugins/PlayPlugin/src/playback/QtPlaybackAdapter.cpp`
- Modify: `plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp`
- Modify: `tests/media_sdk_core_architecture_checks.py`
- Modify: `tests/playplugin_qt_playback_adapter_checks.py`

- [ ] **Step 1: Add failing checks that frame event payloads are gone**

In `tests/media_sdk_core_architecture_checks.py`:

```python
media_events = SDK_ROOT / "include" / "media_sdk" / "MediaEvents.h"
media_events_text = read(media_events)
for forbidden in ("AudioFrameEvent", "VideoFrameEvent"):
    require(forbidden not in media_events_text,
            f"MediaEvents.h must not expose frame payload {forbidden}")
```

In PlayPlugin adapter check:

```python
for path in (
    "plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp",
    "plugins/PlayPlugin/src/playback/QtPlaybackAdapter.cpp",
):
    text = read(path)
    require("AudioFrameEvent" not in text and "VideoFrameEvent" not in text,
            f"{path} must not depend on frame events")
```

- [ ] **Step 2: Run architecture checks and verify failure**

Run:

```bash
python3 tests/media_sdk_core_architecture_checks.py
python3 tests/playplugin_qt_playback_adapter_checks.py
```

Expected: FAIL because frame event payload types or adapter references still exist.

- [ ] **Step 3: Remove frame payload types**

In `MediaEvents.h`, delete:

```cpp
struct AudioFrameEvent {
    AudioFrame frame;
};

struct VideoFrameEvent {
    VideoFrame frame;
};
```

Remove both from `PlayerEventPayload`.

- [ ] **Step 4: Remove adapter branches referencing frame events**

Delete `std::get_if<AudioFrameEvent>` and `std::get_if<VideoFrameEvent>` branches from both adapters. Frame delivery must use `IDecodeFrameSink` only.

- [ ] **Step 5: Verify Task 7**

Run:

```bash
cmake --build build --target MediaSdkCoreFrameContractTest PlayPlugin --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_core_frame_contract|media_sdk_core_architecture_checks|playplugin_qt_playback_adapter_checks"
```

Expected: PASS.

- [ ] **Step 6: Commit Task 7**

```bash
git add sdk/media_core/include/media_sdk/MediaEvents.h sdk/media_core/tests/tst_media_core_frame_contract.cpp plugins/PlayPlugin/src/playback/QtPlaybackAdapter.cpp plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp tests/media_sdk_core_architecture_checks.py tests/playplugin_qt_playback_adapter_checks.py
git commit -m "[架构修改] 移除帧事件兼容路径"
```

Stop and ask the user to confirm Task 8.

---

## Task 8: Final Diagnostics And Verification Gate

**Purpose:** Ensure the frame channel change is observable and did not regress playback contracts.

**Files:**
- Modify: `sdk/media_core/src/DecodeWorker.h/.cpp`
- Modify: `sdk/media_playback_runtime/include/media_sdk/runtime/RuntimeTypes.h`
- Modify: `sdk/media_playback_runtime/src/RuntimePlayer.cpp`
- Modify: relevant tests if diagnostics names require assertions

- [ ] **Step 1: Add decode frame push diagnostics**

Add SDK core counters in `DecodeWorker`:

```cpp
struct DecodeFramePushDiagnostics {
    std::int64_t accepted = 0;
    std::int64_t backpressured = 0;
    std::int64_t stale = 0;
    std::int64_t cancelled = 0;
    std::int64_t closed = 0;
    std::int64_t waitUs = 0;
    std::int64_t maxWaitUs = 0;
};
```

Update in `handleFramePushResult()`:

```cpp
void DecodeWorker::recordFramePushResult(DecodeFramePushResult result)
{
    m_framePushDiagnostics.waitUs += result.waitTime.count();
    m_framePushDiagnostics.maxWaitUs = std::max(
        m_framePushDiagnostics.maxWaitUs,
        result.waitTime.count());
    switch (result.status) {
    case DecodeFramePushStatus::Accepted:
        ++m_framePushDiagnostics.accepted;
        break;
    case DecodeFramePushStatus::Backpressured:
        ++m_framePushDiagnostics.backpressured;
        break;
    case DecodeFramePushStatus::StaleGeneration:
        ++m_framePushDiagnostics.stale;
        break;
    case DecodeFramePushStatus::Cancelled:
        ++m_framePushDiagnostics.cancelled;
        break;
    case DecodeFramePushStatus::Closed:
        ++m_framePushDiagnostics.closed;
        break;
    }
}
```

- [ ] **Step 2: Add runtime diagnostics assertions**

In runtime mock tests, assert:

```cpp
const auto diagnostics = player.diagnostics();
assert(diagnostics.audioQueueHighWatermark > 0 || diagnostics.videoQueueHighWatermark > 0);
```

For a backpressured queue test, assert:

```cpp
assert(diagnostics.decodeFramePushWaitUs > 0);
assert(diagnostics.audioBackpressureCount > 0 || diagnostics.videoBackpressureCount > 0);
```

- [ ] **Step 3: Build all affected targets**

Run:

```bash
cmake --build build --target MediaSdkCoreFrameContractTest MediaSdkCorePlaybackWorkerTest MediaSdkPlaybackRuntimeFrameQueueTest MediaSdkPlaybackRuntimePlayerMockTest PlayPlugin --parallel
```

Expected: all targets build.

- [ ] **Step 4: Run focused CTest gate**

Run:

```bash
ctest --test-dir build --output-on-failure -R "media_sdk_core|media_sdk_playback_runtime|playplugin_qt_playback_adapter_checks"
```

Expected: all matching tests pass.

- [ ] **Step 5: Run full build and full CTest**

Run:

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: full build passes and all tests pass.

- [ ] **Step 6: Manual verification checklist**

Run the app:

```bash
./build/app/PluginBasedApp
```

Verify manually:

- Open `/Users/zs/Downloads/6月29日.mov`.
- Playback starts with video and normal audio.
- 4K/60 playback does not regress compared with current baseline.
- Random seek does not freeze video.
- Pause/resume does not deadlock.
- Stop/open another file does not crash.
- Logs show frame push backpressure counters and queue high watermark when pressure occurs.

- [ ] **Step 7: Commit Task 8**

```bash
git add sdk/media_core/src/DecodeWorker.h sdk/media_core/src/DecodeWorker.cpp sdk/media_playback_runtime/include/media_sdk/runtime/RuntimeTypes.h sdk/media_playback_runtime/src/RuntimePlayer.cpp sdk/media_playback_runtime/tests/tst_runtime_player_mock.cpp
git commit -m "[性能观测] 增加解码帧通道背压诊断"
```

Stop and report final verification results to the user.

---

## Implementation Notes

- `QtPlaybackAdapter` is still part of the build, so implement `IDecodeFrameSink` there by converting SDK frames to existing `PlaybackDataBridge` queues. Do not keep old frame events.
- If EOF ordering shows regressions, do not add timer retries. Promote EOF to the frame channel as a formal drain marker in a follow-up design.
- If a queue push wait becomes long during manual 4K/60 testing, inspect diagnostics before changing capacities. Capacity increases require evidence and should be a separate commit.
