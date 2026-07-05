# Media SDK Seek Resolution Clock Anchor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prevent seek progress from jumping from requested target to first audio PTS by adding runtime seek clock anchoring, bounded audio gap fill, and seek resolution diagnostics.

**Architecture:** Keep UI passive. Core resolves target-side media boundaries, runtime owns authoritative playback clock and audio gap fill, session forwards only generation-safe runtime position. The first implementation phase fixes clock continuity without changing public API; later tasks extend `SeekCompletedEvent` with compatible resolution fields.

**Tech Stack:** C++20, FFmpeg-backed media core, `media_sdk_playback_runtime`, `media_sdk_playback_session`, CoreAudio ring buffer, CMake/CTest assert-based tests.

---

## Execution Rules

- Execute tasks in order.
- Commit after each completed task if verification passes.
- Do not fix the symptom in QML.
- Do not revert `9609fec [稳定性修复] 修复seek完成时序死锁`.
- Keep public `SeekCompletedEvent::position` compatible.
- Remove or gate temporary `SeekTrace` logs before final verification.
- Use focused tests first, then broader runtime/session/core tests, then full build.

## Current Evidence

Manual log in `log.txt` showed:

```text
seek target = 4763ms
runtime afterFlush clock = 0ms
firstAudioAfterSeek pts = 6035ms
first forwarded position = 6035ms
UI previous position = 4763ms
```

The implementation must make this path produce a stable target-side position instead of jumping to `6035ms`.

## File Responsibility Map

### Create

- `sdk/media_playback_runtime/src/AudioSilence.h`
  - Generates bounded silence bytes for runtime audio formats.
- `sdk/media_playback_runtime/src/SeekClockAnchor.h`
  - Tracks seek target, generation, first audio PTS, and gap policy state.
- `sdk/media_playback_runtime/tests/tst_runtime_audio_silence.cpp`
  - Unit tests for silence generation.
- `sdk/media_playback_runtime/tests/tst_seek_clock_anchor.cpp`
  - Unit tests for anchor/gap decisions.

### Modify

- `sdk/media_playback_runtime/CMakeLists.txt`
  - Register new helper tests.
- `sdk/media_playback_runtime/src/RuntimePlayer.cpp`
  - Store seek anchor in `seek(position)`.
  - Fill bounded silence before first post-seek audio write when needed.
  - Remove temporary `SeekTrace` diagnostics or gate them behind a disabled compile-time flag.
- `sdk/media_playback_runtime/tests/tst_runtime_player_mock.cpp`
  - Add runtime integration tests for seek audio gap fill.
- `sdk/media_playback_session/src/SessionEventRouter.h`
  - Remove temporary `SeekTrace` diagnostics.
  - Keep position gate semantics.
- `sdk/media_playback_session/tests/tst_playback_session_event_router.cpp`
  - Add regression for target-side runtime clock after seek.
- `sdk/media_core/include/media_sdk/MediaEvents.h`
  - Later task: append seek resolution fields to `SeekCompletedEvent`.
- `sdk/media_core/src/SeekPrerollGate.h`
  - Later task: expose first accepted audio/video PTS.
- `sdk/media_core/src/DecodeWorker.cpp`
  - Later task: populate seek resolution fields.
- `plugins/PlayPlugin/src/playback/PlayerEngine.cpp`
  - Remove temporary `SeekTrace` diagnostics.
- `plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp`
  - Remove temporary `SeekTrace` diagnostics.
- `log.txt`
  - Do not commit; local diagnostic artifact only.

## Verification Commands

Use as applicable:

```bash
cmake --build build --target MediaSdkPlaybackRuntimePlayerMockTest --parallel
ctest --test-dir build -R media_sdk_playback_runtime_player_mock --output-on-failure
cmake --build build --target MediaSdkPlaybackRuntimeAudioSilenceTest MediaSdkPlaybackRuntimeSeekClockAnchorTest --parallel
ctest --test-dir build -R "media_sdk_playback_runtime_audio_silence|media_sdk_playback_runtime_seek_clock_anchor" --output-on-failure
cmake --build build --target MediaSdkPlaybackSessionEventRouterTest --parallel
ctest --test-dir build -R media_sdk_playback_session_event_router --output-on-failure
cmake --build build --target MediaSdkCorePlaybackWorkerTest --parallel
ctest --test-dir build -R media_sdk_core_playback_worker --output-on-failure
ctest --test-dir build -R "playplugin_sdk_playback_adapter|media_sdk_core_playback_worker|media_sdk_playback_runtime_player_mock|media_sdk_playback_session_event_router" --output-on-failure
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

---

## Task 1: Runtime Regression Test For Seek Audio Gap Jump

**Purpose:** Lock the observed bug before changing runtime behavior.

**Files:**
- Modify: `sdk/media_playback_runtime/tests/tst_runtime_player_mock.cpp`

- [ ] **Step 1: Add a clocking mock audio output test seam**

In `tst_runtime_player_mock.cpp`, add a helper near the existing `MockAudioOutput` or extend it with an opt-in mode:

```cpp
bool clockFromWrites = false;
bool resetClockToZeroOnFlush = false;
bool firstWriteAfterFlush = false;

media_sdk::Result<void> write(media_sdk::runtime::AudioBufferView buffer) override
{
    std::unique_lock lock(mutex);
    if (blockWrites) {
        writeBlocked = true;
        cv.notify_all();
        cv.wait(lock, [this]() { return !blockWrites; });
    }
    ++writeCount;
    writtenBytes += buffer.bytes.size();
    lastWrittenBytes.assign(buffer.bytes.begin(), buffer.bytes.end());
    lastWritePts = buffer.pts;
    lastWriteGeneration = buffer.generation;
    if (clockFromWrites) {
        if (firstWriteAfterFlush || !snapshot.valid || snapshot.generation != buffer.generation) {
            snapshot.position = buffer.pts;
            firstWriteAfterFlush = false;
        }
        snapshot.generation = buffer.generation;
        snapshot.valid = true;
    }
    return media_sdk::Result<void>::success();
}

media_sdk::Result<void> flush() override
{
    {
        std::lock_guard lock(mutex);
        ++flushCount;
        if (failFlush) {
            return media_sdk::Result<void>::failure({
                .code = media_sdk::MediaErrorCode::InternalStateError,
                .message = "audio flush failed",
                .detail = {},
            });
        }
        blockWrites = false;
        ++snapshot.generation;
        if (resetClockToZeroOnFlush) {
            snapshot.position = 0us;
            firstWriteAfterFlush = true;
        }
    }
    cv.notify_all();
    return media_sdk::Result<void>::success();
}
```

- [ ] **Step 2: Add failing regression test**

Add:

```cpp
void seekAudioGapDoesNotExposeFirstAudioPtsAsImmediateClock()
{
    MockAudioOutput audio;
    audio.clockFromWrites = true;
    audio.resetClockToZeroOnFlush = true;
    audio.setClock(1500ms, 1);
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    player.seek(4763ms);
    assert(player.timeline().generation == 2);

    discardFramePushResult(player.enqueueAudio(runtimeAudio(1, 2, 6035ms)));
    assert(waitUntil([&audio]() { return audio.writeCount >= 1; }));

    const auto clock = player.clock();
    assert(clock.valid);
    assert(clock.generation == 2);
    assert(clock.position < 6035ms);
    assert(clock.position >= 4763ms);
}
```

Register it in `main()`:

```cpp
seekAudioGapDoesNotExposeFirstAudioPtsAsImmediateClock();
```

- [ ] **Step 3: Run red**

Run:

```bash
cmake --build build --target MediaSdkPlaybackRuntimePlayerMockTest --parallel
ctest --test-dir build -R media_sdk_playback_runtime_player_mock --output-on-failure
```

Expected: FAIL because runtime clock jumps to `6035ms`.

- [ ] **Step 4: Commit only if this is a deliberate red-test commit**

Normally do not commit red tests alone unless the user explicitly asks. Continue to Task 2 in the same working set.

---

## Task 2: Add AudioSilence Helper

**Purpose:** Generate deterministic silence bytes for bounded seek audio gap fill.

**Files:**
- Create: `sdk/media_playback_runtime/src/AudioSilence.h`
- Create: `sdk/media_playback_runtime/tests/tst_runtime_audio_silence.cpp`
- Modify: `sdk/media_playback_runtime/CMakeLists.txt`

- [ ] **Step 1: Write silence helper tests**

Create `sdk/media_playback_runtime/tests/tst_runtime_audio_silence.cpp`:

```cpp
#include "AudioSilence.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>

using namespace std::chrono_literals;

namespace {

void int16SilenceIsZeroFilled()
{
    const auto silence = media_sdk::runtime::makeSilenceBytes({
        .sampleRate = 1000,
        .channels = 2,
        .sampleFormat = media_sdk::runtime::AudioSampleFormat::Int16,
    }, 2ms);

    assert(silence.ok());
    assert(silence.value().size() == 2 * 2 * sizeof(std::int16_t));
    for (const auto byte : silence.value())
        assert(byte == std::byte { 0 });
}

void uint8SilenceUsesUnsignedCenter()
{
    const auto silence = media_sdk::runtime::makeSilenceBytes({
        .sampleRate = 1000,
        .channels = 1,
        .sampleFormat = media_sdk::runtime::AudioSampleFormat::UInt8,
    }, 3ms);

    assert(silence.ok());
    assert(silence.value().size() == 3);
    for (const auto byte : silence.value())
        assert(byte == std::byte { 0x80 });
}

void invalidFormatFails()
{
    const auto silence = media_sdk::runtime::makeSilenceBytes({
        .sampleRate = 0,
        .channels = 2,
        .sampleFormat = media_sdk::runtime::AudioSampleFormat::Float32,
    }, 1ms);

    assert(!silence.ok());
}

} // namespace

int main()
{
    int16SilenceIsZeroFilled();
    uint8SilenceUsesUnsignedCenter();
    invalidFormatFails();
}
```

- [ ] **Step 2: Register test target**

Add under `if(BUILD_TESTING)` in `sdk/media_playback_runtime/CMakeLists.txt`:

```cmake
add_executable(MediaSdkPlaybackRuntimeAudioSilenceTest
    tests/tst_runtime_audio_silence.cpp
)
target_compile_features(MediaSdkPlaybackRuntimeAudioSilenceTest PRIVATE cxx_std_20)
set_target_properties(MediaSdkPlaybackRuntimeAudioSilenceTest PROPERTIES
    CXX_EXTENSIONS OFF
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_include_directories(MediaSdkPlaybackRuntimeAudioSilenceTest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(MediaSdkPlaybackRuntimeAudioSilenceTest PRIVATE
    media_sdk_playback_runtime
)
add_test(NAME media_sdk_playback_runtime_audio_silence
    COMMAND $<TARGET_FILE:MediaSdkPlaybackRuntimeAudioSilenceTest>
)
```

- [ ] **Step 3: Run red**

Run:

```bash
cmake --build build --target MediaSdkPlaybackRuntimeAudioSilenceTest --parallel
```

Expected: FAIL because `AudioSilence.h` does not exist.

- [ ] **Step 4: Implement helper**

Create `sdk/media_playback_runtime/src/AudioSilence.h`:

```cpp
#pragma once

#include "media_sdk/Error.h"
#include "media_sdk/Result.h"
#include "media_sdk/runtime/AudioOutput.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace media_sdk::runtime {

inline std::size_t bytesPerSample(AudioSampleFormat format)
{
    switch (format) {
    case AudioSampleFormat::UInt8:
        return 1;
    case AudioSampleFormat::Int16:
        return 2;
    case AudioSampleFormat::Int32:
    case AudioSampleFormat::Float32:
        return 4;
    case AudioSampleFormat::Float32Planar:
    case AudioSampleFormat::Unknown:
        return 0;
    }
    return 0;
}

inline Result<std::vector<std::byte>> makeSilenceBytes(
    AudioFormat format,
    std::chrono::microseconds duration)
{
    if (duration <= std::chrono::microseconds { 0 })
        return Result<std::vector<std::byte>>::success({});

    const auto sampleBytes = bytesPerSample(format.sampleFormat);
    if (sampleBytes == 0 || format.sampleRate <= 0 || format.channels <= 0) {
        return Result<std::vector<std::byte>>::failure({
            .code = MediaErrorCode::InternalStateError,
            .message = "Cannot generate silence for invalid audio format",
            .detail = {},
        });
    }

    const auto frames = static_cast<std::int64_t>(
        (duration.count() * static_cast<std::int64_t>(format.sampleRate) + 999999) / 1000000);
    const auto byteCount = static_cast<std::size_t>(frames)
        * static_cast<std::size_t>(format.channels)
        * sampleBytes;

    std::vector<std::byte> bytes(byteCount, std::byte { 0 });
    if (format.sampleFormat == AudioSampleFormat::UInt8) {
        for (auto& byte : bytes)
            byte = std::byte { 0x80 };
    }
    return Result<std::vector<std::byte>>::success(std::move(bytes));
}

} // namespace media_sdk::runtime
```

- [ ] **Step 5: Run green**

Run:

```bash
cmake --build build --target MediaSdkPlaybackRuntimeAudioSilenceTest --parallel
ctest --test-dir build -R media_sdk_playback_runtime_audio_silence --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add sdk/media_playback_runtime/src/AudioSilence.h \
        sdk/media_playback_runtime/tests/tst_runtime_audio_silence.cpp \
        sdk/media_playback_runtime/CMakeLists.txt
git commit -m "[稳定性修复] 增加seek静音填充工具"
```

---

## Task 3: Add SeekClockAnchor Helper

**Purpose:** Encapsulate seek target, generation, gap tolerance, and first-audio decision outside `RuntimePlayer.cpp`.

**Files:**
- Create: `sdk/media_playback_runtime/src/SeekClockAnchor.h`
- Create: `sdk/media_playback_runtime/tests/tst_seek_clock_anchor.cpp`
- Modify: `sdk/media_playback_runtime/CMakeLists.txt`

- [ ] **Step 1: Write anchor tests**

Create `sdk/media_playback_runtime/tests/tst_seek_clock_anchor.cpp`:

```cpp
#include "SeekClockAnchor.h"

#include <cassert>
#include <chrono>

using namespace std::chrono_literals;

namespace {

void firstAudioWithinToleranceDoesNotNeedGap()
{
    media_sdk::runtime::SeekClockAnchor anchor;
    anchor.begin(2, 4763ms);

    const auto decision = anchor.inspectFirstAudio(2, 4780ms);

    assert(decision.current);
    assert(!decision.fillGap);
}

void firstAudioAfterTargetNeedsBoundedGap()
{
    media_sdk::runtime::SeekClockAnchor anchor;
    anchor.begin(2, 4763ms);

    const auto decision = anchor.inspectFirstAudio(2, 6035ms);

    assert(decision.current);
    assert(decision.fillGap);
    assert(decision.gapStart == 4763ms);
    assert(decision.gapDuration == 1272ms);
}

void staleGenerationDoesNotUseAnchor()
{
    media_sdk::runtime::SeekClockAnchor anchor;
    anchor.begin(2, 4763ms);

    const auto decision = anchor.inspectFirstAudio(1, 6035ms);

    assert(!decision.current);
    assert(!decision.fillGap);
}

void largeGapIsNotFilled()
{
    media_sdk::runtime::SeekClockAnchor anchor;
    anchor.begin(2, 1000ms);

    const auto decision = anchor.inspectFirstAudio(2, 5000ms);

    assert(decision.current);
    assert(!decision.fillGap);
    assert(decision.exceedsMaxGap);
}

} // namespace

int main()
{
    firstAudioWithinToleranceDoesNotNeedGap();
    firstAudioAfterTargetNeedsBoundedGap();
    staleGenerationDoesNotUseAnchor();
    largeGapIsNotFilled();
}
```

- [ ] **Step 2: Register test target**

Add under `if(BUILD_TESTING)`:

```cmake
add_executable(MediaSdkPlaybackRuntimeSeekClockAnchorTest
    tests/tst_seek_clock_anchor.cpp
)
target_compile_features(MediaSdkPlaybackRuntimeSeekClockAnchorTest PRIVATE cxx_std_20)
set_target_properties(MediaSdkPlaybackRuntimeSeekClockAnchorTest PROPERTIES
    CXX_EXTENSIONS OFF
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_include_directories(MediaSdkPlaybackRuntimeSeekClockAnchorTest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(MediaSdkPlaybackRuntimeSeekClockAnchorTest PRIVATE
    media_sdk_playback_runtime
)
add_test(NAME media_sdk_playback_runtime_seek_clock_anchor
    COMMAND $<TARGET_FILE:MediaSdkPlaybackRuntimeSeekClockAnchorTest>
)
```

- [ ] **Step 3: Run red**

Run:

```bash
cmake --build build --target MediaSdkPlaybackRuntimeSeekClockAnchorTest --parallel
```

Expected: FAIL because `SeekClockAnchor.h` does not exist.

- [ ] **Step 4: Implement helper**

Create `sdk/media_playback_runtime/src/SeekClockAnchor.h`:

```cpp
#pragma once

#include "media_sdk/runtime/RuntimeTypes.h"

#include <chrono>

namespace media_sdk::runtime {

struct SeekAudioGapDecision {
    bool current = false;
    bool fillGap = false;
    bool exceedsMaxGap = false;
    std::chrono::microseconds gapStart { 0 };
    std::chrono::microseconds gapDuration { 0 };
};

class SeekClockAnchor
{
public:
    void begin(Generation generation, std::chrono::microseconds requestedPosition)
    {
        m_active = true;
        m_generation = generation;
        m_requestedPosition = requestedPosition;
    }

    void clear()
    {
        m_active = false;
        m_generation = 0;
        m_requestedPosition = std::chrono::microseconds { 0 };
    }

    SeekAudioGapDecision inspectFirstAudio(
        Generation generation,
        std::chrono::microseconds firstAudioPts)
    {
        if (!m_active || generation != m_generation)
            return {};

        SeekAudioGapDecision decision {
            .current = true,
            .gapStart = m_requestedPosition,
        };

        if (firstAudioPts <= m_requestedPosition + m_gapTolerance) {
            clear();
            return decision;
        }

        const auto gap = firstAudioPts - m_requestedPosition;
        decision.gapDuration = gap;
        if (gap <= m_maxGapFill)
            decision.fillGap = true;
        else
            decision.exceedsMaxGap = true;
        clear();
        return decision;
    }

private:
    bool m_active = false;
    Generation m_generation = 0;
    std::chrono::microseconds m_requestedPosition { 0 };
    std::chrono::microseconds m_gapTolerance { 50'000 };
    std::chrono::microseconds m_maxGapFill { 2'000'000 };
};

} // namespace media_sdk::runtime
```

- [ ] **Step 5: Run green**

Run:

```bash
cmake --build build --target MediaSdkPlaybackRuntimeSeekClockAnchorTest --parallel
ctest --test-dir build -R media_sdk_playback_runtime_seek_clock_anchor --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add sdk/media_playback_runtime/src/SeekClockAnchor.h \
        sdk/media_playback_runtime/tests/tst_seek_clock_anchor.cpp \
        sdk/media_playback_runtime/CMakeLists.txt
git commit -m "[稳定性修复] 增加seek时钟锚点模型"
```

---

## Task 4: Fill Bounded Silence Before First Post-Seek Audio

**Purpose:** Make the Task 1 regression pass by preventing first audio PTS from becoming an immediate clock jump.

**Files:**
- Modify: `sdk/media_playback_runtime/src/RuntimePlayer.cpp`
- Modify: `sdk/media_playback_runtime/tests/tst_runtime_player_mock.cpp`

- [ ] **Step 1: Include helpers**

In `RuntimePlayer.cpp`:

```cpp
#include "AudioSilence.h"
#include "SeekClockAnchor.h"
```

- [ ] **Step 2: Add runtime state**

Inside `RuntimePlayer::Impl` members:

```cpp
SeekClockAnchor seekClockAnchor;
```

- [ ] **Step 3: Begin anchor in seek**

In `RuntimePlayer::Impl::seek(std::chrono::microseconds position)`, after `nextGeneration = ++generation;` and before releasing the lock:

```cpp
seekClockAnchor.begin(nextGeneration, position);
```

Also clear it in `stop()` and `open()` reset paths where generation/session resets:

```cpp
seekClockAnchor.clear();
```

- [ ] **Step 4: Add helper to write silence gap**

Add private method in `RuntimePlayer::Impl`:

```cpp
Result<void> writeSeekGapSilence(
    std::chrono::microseconds gapStart,
    std::chrono::microseconds gapDuration,
    Generation generation)
{
    constexpr auto maxChunk = std::chrono::milliseconds { 100 };
    auto remaining = gapDuration;
    auto pts = gapStart;
    while (remaining > std::chrono::microseconds { 0 }) {
        const auto chunk = std::min(remaining, std::chrono::duration_cast<std::chrono::microseconds>(maxChunk));
        auto silence = makeSilenceBytes(config.audioFormat, chunk);
        if (!silence.ok())
            return Result<void>::failure(silence.error());

        const auto writeResult = dependencies.audioOutput->write({
            .bytes = silence.value(),
            .pts = pts,
            .generation = generation,
        });
        if (!writeResult.ok())
            return writeResult;

        pts += chunk;
        remaining -= chunk;
    }
    return Result<void>::success();
}
```

- [ ] **Step 5: Apply gap before first real audio write**

In `audioLoop()`, before writing `queuedFrame.frame.samples()`:

```cpp
const auto framePts = queuedFrame.frame.pts();
const auto frameGeneration = queuedFrame.generation;
const auto gapDecision = seekClockAnchor.inspectFirstAudio(frameGeneration, framePts);
if (gapDecision.current && gapDecision.fillGap) {
    const auto silenceResult = writeSeekGapSilence(
        gapDecision.gapStart,
        gapDecision.gapDuration,
        frameGeneration);
    if (!silenceResult.ok()) {
        reportRuntimeError(silenceResult.error());
        continue;
    }
}
```

Keep the real audio write after the silence fill.

- [ ] **Step 6: Run focused regression**

Run:

```bash
cmake --build build --target MediaSdkPlaybackRuntimePlayerMockTest --parallel
ctest --test-dir build -R media_sdk_playback_runtime_player_mock --output-on-failure
```

Expected: PASS, including Task 1 regression.

- [ ] **Step 7: Run helper tests**

Run:

```bash
ctest --test-dir build -R "media_sdk_playback_runtime_audio_silence|media_sdk_playback_runtime_seek_clock_anchor" --output-on-failure
```

Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add sdk/media_playback_runtime/src/RuntimePlayer.cpp \
        sdk/media_playback_runtime/tests/tst_runtime_player_mock.cpp
git commit -m "[稳定性修复] 使用seek锚点填充音频空洞"
```

---

## Task 5: Session Regression For Stable Target-Side Position

**Purpose:** Verify session keeps forwarding runtime clock, but runtime clock no longer jumps past target immediately.

**Files:**
- Modify: `sdk/media_playback_session/tests/tst_playback_session_event_router.cpp`
- Modify: `sdk/media_playback_session/src/SessionEventRouter.h`

- [ ] **Step 1: Remove temporary `SeekTrace` from session**

Remove `#include <cstdio>` and every `std::fprintf(stderr, "SeekTrace: ...")` in `SessionEventRouter.h`.

- [ ] **Step 2: Add position test**

In `tst_playback_session_event_router.cpp`, add:

```cpp
void seekForwardsAnchoredRuntimeClockAtTarget()
{
    media_sdk::session::SessionTimeline timeline;
    RecordingRuntimeControl runtime;
    RecordingSessionEvents events;
    media_sdk::session::SessionEventRouter<RecordingRuntimeControl> router(
        runtime,
        timeline,
        &events);

    router.onEvent(mediaInfoEvent(coreTimeline(10, 3)));
    router.beginSeek(runtimeTimeline(21, 8), 4763ms);
    runtime.nextClock.generation = 8;
    events.events.clear();

    router.onEvent(seekCompletedEvent(coreTimeline(10, 4), 4763ms));
    assert(events.events.size() == 1);

    runtime.nextClock.position = 4763ms;
    router.onEvent(positionEvent(coreTimeline(10, 4), 6035ms));

    assert(events.events.size() == 2);
    const auto* position =
        std::get_if<media_sdk::PositionChangedEvent>(&events.events.back().payload);
    assert(position);
    assert(position->position == 4763ms);
}
```

Register in `main()`:

```cpp
seekForwardsAnchoredRuntimeClockAtTarget();
```

- [ ] **Step 3: Run session tests**

Run:

```bash
cmake --build build --target MediaSdkPlaybackSessionEventRouterTest --parallel
ctest --test-dir build -R media_sdk_playback_session_event_router --output-on-failure
```

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add sdk/media_playback_session/src/SessionEventRouter.h \
        sdk/media_playback_session/tests/tst_playback_session_event_router.cpp
git commit -m "[稳定性修复] 验证seek后转发锚定时钟"
```

---

## Task 6: Extend SeekCompletedEvent With Compatible Resolution Fields

**Purpose:** Let SDK explain seek actual media boundaries without breaking current `position` users.

**Files:**
- Modify: `sdk/media_core/include/media_sdk/MediaEvents.h`
- Modify: `sdk/media_core/src/SeekPrerollGate.h`
- Modify: `sdk/media_core/src/DecodeWorker.cpp`
- Modify: `sdk/media_core/tests/tst_media_core_playback_worker.cpp`
- Modify: `plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp`

- [ ] **Step 1: Extend event struct**

In `MediaEvents.h`, append fields:

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

Also add:

```cpp
#include <optional>
```

- [ ] **Step 2: Record first accepted PTS in gate**

In `SeekPrerollGate.h`, add members:

```cpp
std::optional<std::chrono::microseconds> m_firstVideoPts;
std::optional<std::chrono::microseconds> m_firstAudioPts;
```

Add methods:

```cpp
void markVideoAccepted(std::chrono::microseconds pts)
{
    m_videoReady = true;
    if (!m_firstVideoPts)
        m_firstVideoPts = pts;
}

void markAudioAccepted(std::chrono::microseconds pts)
{
    m_audioReady = true;
    if (!m_firstAudioPts)
        m_firstAudioPts = pts;
}

std::optional<std::chrono::microseconds> firstVideoPts() const
{
    return m_firstVideoPts;
}

std::optional<std::chrono::microseconds> firstAudioPts() const
{
    return m_firstAudioPts;
}
```

Keep the existing no-arg `markVideoAccepted()` and `markAudioAccepted()` as wrappers if existing tests need them:

```cpp
void markVideoAccepted()
{
    markVideoAccepted(m_config.target);
}

void markAudioAccepted()
{
    markAudioAccepted(m_config.target);
}
```

- [ ] **Step 3: Populate event in DecodeWorker**

In `emitSeekCompletedIfReady()`, build:

```cpp
const auto requested = std::chrono::duration_cast<std::chrono::milliseconds>(
    m_pendingSeekTarget.value_or(m_seekGate->completionPosition()));
const auto firstAudio = m_seekGate->firstAudioPts().transform(
    [](auto pts) { return std::chrono::duration_cast<std::chrono::milliseconds>(pts); });
const auto firstVideo = m_seekGate->firstVideoPts().transform(
    [](auto pts) { return std::chrono::duration_cast<std::chrono::milliseconds>(pts); });
const bool audioGap = firstAudio.has_value() && *firstAudio > requested + std::chrono::milliseconds { 50 };
emitEvent(makeEvent(SeekCompletedEvent {
    .position = requested,
    .requestedPosition = requested,
    .firstAudioPts = firstAudio,
    .firstVideoPts = firstVideo,
    .exact = true,
    .audioGap = audioGap,
}));
```

If `std::optional::transform` is unavailable in the configured standard library, use local helper conversion functions.

Update calls:

```cpp
m_seekGate->markVideoAccepted(std::chrono::microseconds { frame->pts });
m_seekGate->markAudioAccepted(audioFrame.pts());
```

- [ ] **Step 4: Keep adapter mapping by position**

In `SdkPlaybackAdapter.cpp`, keep:

```cpp
qtGeneration = m_pendingSeekRequests.takeForCompletedPosition(payload->position);
```

Do not switch pending seek mapping to `firstAudioPts`.

- [ ] **Step 5: Add core test assertion**

In `tst_media_core_playback_worker.cpp`, extend an existing seek completion test:

```cpp
const auto* seekCompleted = findSeekCompleted(events);
assert(seekCompleted);
const auto* payload = std::get_if<media_sdk::SeekCompletedEvent>(&seekCompleted->payload);
assert(payload);
assert(payload->requestedPosition == payload->position);
```

- [ ] **Step 6: Run focused tests**

Run:

```bash
cmake --build build --target MediaSdkCorePlaybackWorkerTest PlayPluginSdkPlaybackAdapterTest --parallel
ctest --test-dir build -R "media_sdk_core_playback_worker|playplugin_sdk_playback_adapter" --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add sdk/media_core/include/media_sdk/MediaEvents.h \
        sdk/media_core/src/SeekPrerollGate.h \
        sdk/media_core/src/DecodeWorker.cpp \
        sdk/media_core/tests/tst_media_core_playback_worker.cpp \
        plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp
git commit -m "[稳定性修复] 扩展seek完成分辨率信息"
```

---

## Task 7: Remove Temporary SeekTrace Diagnostics

**Purpose:** Clean up local diagnostic logs before final verification.

**Files:**
- Modify: `plugins/PlayPlugin/src/playback/PlayerEngine.cpp`
- Modify: `plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp`
- Modify: `sdk/media_playback_runtime/src/RuntimePlayer.cpp`
- Modify: `sdk/media_playback_session/src/SessionEventRouter.h`
- Delete from git consideration only: `log.txt` remains untracked and must not be committed.

- [ ] **Step 1: Remove PlayPlugin temporary logs**

Remove `LOG_INFO("SeekTrace: ...")` and `LOG_DEBUG("SeekTrace: ...")` added for investigation from:

```text
plugins/PlayPlugin/src/playback/PlayerEngine.cpp
plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp
```

Keep existing non-temporary logs such as:

```cpp
LOG_INFO("PlayerEngine: seek to {}ms", positionMs);
LOG_DEBUG("SdkPlaybackAdapter: ignored unmapped seek completion");
```

- [ ] **Step 2: Remove runtime temporary fprintf**

Remove `#include <cstdio>` and `std::fprintf(stderr, "SeekTrace: ...")` from `RuntimePlayer.cpp`.

- [ ] **Step 3: Remove session temporary fprintf**

If not already removed in Task 5, remove `#include <cstdio>` and all `SeekTrace` prints from `SessionEventRouter.h`.

- [ ] **Step 4: Ensure log.txt is untracked**

Run:

```bash
git status --short
```

Expected: `?? log.txt` may remain, but it must not be added.

- [ ] **Step 5: Build focused targets**

Run:

```bash
cmake --build build --target PluginBasedApp MediaSdkPlaybackRuntimePlayerMockTest MediaSdkPlaybackSessionEventRouterTest --parallel
ctest --test-dir build -R "media_sdk_playback_runtime_player_mock|media_sdk_playback_session_event_router|playplugin_sdk_playback_adapter" --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add plugins/PlayPlugin/src/playback/PlayerEngine.cpp \
        plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp \
        sdk/media_playback_runtime/src/RuntimePlayer.cpp \
        sdk/media_playback_session/src/SessionEventRouter.h
git commit -m "[清理] 移除seek诊断临时日志"
```

---

## Task 8: Full Verification And Manual Reproduction

**Purpose:** Validate the SDK-level fix across current playback surfaces.

**Files:**
- No source changes expected.

- [ ] **Step 1: Run related tests**

Run:

```bash
ctest --test-dir build -R "playplugin_pending_seek_requests|playplugin_sdk_playback_adapter|media_sdk_core_playback_worker|media_sdk_core_seek_preroll_gate|media_sdk_core_seek_audio_trimmer|media_sdk_playback_runtime_player_mock|media_sdk_playback_session_contract|media_sdk_playback_session_event_router|media_sdk_playback_session_fallback|media_sdk_platform_audio_macos" --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 2: Run full build and full test suite**

Run:

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: full build succeeds and all tests pass.

- [ ] **Step 3: Manual seek verification**

Run:

```bash
./build/app/PluginBasedApp
```

Manual checks:

- Open the same media used for `log.txt`.
- Seek around `4763ms`.
- Confirm progress does not jump immediately to `6035ms`.
- Confirm playback resumes without black screen or stuck frame.
- Confirm pause button state remains correct.
- Confirm audio resumes naturally after the gap.

- [ ] **Step 4: Commit verification note if docs are updated**

If no files changed, do not commit. If a manual verification document is added later, commit it separately.

---

## Final Review Checklist

- [ ] No `SeekTrace` remains in source.
- [ ] `log.txt` is not staged.
- [ ] `RuntimePlayer::seek(position)` uses `position`.
- [ ] First post-seek audio gap is filled or explicitly bounded.
- [ ] `SeekCompletedEvent::position` remains compatible for PlayPlugin pending seek mapping.
- [ ] Full `ctest` passes.
- [ ] Manual seek no longer jumps from requested target to first audio PTS.
