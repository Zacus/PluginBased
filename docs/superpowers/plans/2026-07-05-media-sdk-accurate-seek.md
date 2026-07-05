# Media SDK Accurate Seek Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Upgrade `media_sdk::Player::seek(position)` from keyframe-backward seek to SDK-grade accurate seek with target-side video delivery, sample-aligned audio trimming, and delayed seek completion.

**Architecture:** Keep seek precision inside `media_sdk_core::DecodeWorker`. Add small internal helpers for seek gating and audio trimming, then integrate them before decoded frames reach `IDecodeFrameSink`, so session/runtime queues and clocks never see target-before frames.

**Tech Stack:** C++20, FFmpeg demux/decode, SDK `AudioFrame`/`VideoFrame` value contracts, CMake/CTest, existing assert-based SDK tests.

---

## Execution Rules

- Execute one task at a time.
- Each task ends with a focused commit.
- Use TDD: write the failing test, run it red, implement the smallest green change, then run focused tests.
- Do not change PlayPlugin UI, QML, Qt presenter, or runtime scheduling in this plan.
- Do not add a public `SeekOptions` API in Phase 1.
- Do not move filtering into `media_sdk_playback_session` or `media_sdk_playback_runtime`.
- Preserve current `Player::seek(std::chrono::milliseconds)` source compatibility.
- Keep helper files Qt-free and private to `sdk/media_core/src`.

## File Responsibility Map

### Create

- `sdk/media_core/src/SeekPrerollGate.h`
  - Tracks accurate seek target, stream readiness, discard counters, and completion eligibility.
- `sdk/media_core/src/SeekAudioTrimmer.h`
  - Trims interleaved SDK `AudioFrame` samples to the seek target and returns a new owned frame.
- `sdk/media_core/tests/tst_media_core_seek_preroll_gate.cpp`
  - Unit tests for video/audio readiness, discard decisions, and completion rules.
- `sdk/media_core/tests/tst_media_core_seek_audio_trimmer.cpp`
  - Unit tests for sample-aligned trimming across supported interleaved formats.

### Modify

- `sdk/media_core/CMakeLists.txt`
  - Add the two new test executables and include private helper headers.
- `sdk/media_core/src/DecodeWorker.h`
  - Add pending accurate seek state and helper method declarations.
- `sdk/media_core/src/DecodeWorker.cpp`
  - Replace immediate seek completion with accurate preroll completion.
  - Gate decoded video before `VideoFrameProcessor::process()`.
  - Gate and trim decoded audio before `m_frames.pushAudio()`.
- `sdk/media_core/tests/tst_media_core_playback_worker.cpp`
  - Add integration tests for playing seek, paused seek, burst seek, and EOF-adjacent seek.
- `sdk/media_playback_session/tests/tst_playback_session_contract.cpp`
  - Verify delayed seek completion still maps core/runtime timelines correctly.
- `sdk/media_playback_runtime/tests/tst_runtime_player_mock.cpp`
  - Add regression only if runtime receives target-before audio/video in a focused test seam.

## Verification Commands

Use these commands during the plan:

```bash
cmake --build build --target MediaSdkCoreSeekPrerollGateTest --parallel
ctest --test-dir build -R media_sdk_core_seek_preroll_gate --output-on-failure
cmake --build build --target MediaSdkCoreSeekAudioTrimmerTest --parallel
ctest --test-dir build -R media_sdk_core_seek_audio_trimmer --output-on-failure
cmake --build build --target MediaSdkCorePlaybackWorkerTest --parallel
ctest --test-dir build -R media_sdk_core_playback_worker --output-on-failure
ctest --test-dir build -R "media_sdk_core|media_sdk_playback_session|media_sdk_playback_runtime" --output-on-failure
cmake --build build --parallel
```

If `build/` is missing, configure using the command documented in `AGENTS.md`.

---

## Task 1: Add SeekPrerollGate Unit Contract

**Purpose:** Define accurate seek gating behavior before touching `DecodeWorker`.

**Files:**
- Create: `sdk/media_core/src/SeekPrerollGate.h`
- Create: `sdk/media_core/tests/tst_media_core_seek_preroll_gate.cpp`
- Modify: `sdk/media_core/CMakeLists.txt`

- [ ] **Step 1: Write failing gate tests**

Create `sdk/media_core/tests/tst_media_core_seek_preroll_gate.cpp`:

```cpp
#include "SeekPrerollGate.h"

#include <cassert>
#include <chrono>

using namespace std::chrono_literals;

namespace {

void videoBeforeTargetIsDiscarded()
{
    media_sdk::SeekPrerollGate gate({
        .target = 1000ms,
        .generation = 2,
        .hasVideo = true,
        .hasAudio = false,
    });

    const auto decision = gate.inspectVideo(900ms, 2);

    assert(decision.action == media_sdk::SeekPrerollAction::Discard);
    assert(!gate.shouldEmitCompletion());
}

void videoAtTargetIsAcceptedAndCompletesVideoOnlySeek()
{
    media_sdk::SeekPrerollGate gate({
        .target = 1000ms,
        .generation = 2,
        .hasVideo = true,
        .hasAudio = false,
    });

    const auto decision = gate.inspectVideo(1000ms, 2);
    assert(decision.action == media_sdk::SeekPrerollAction::Accept);
    gate.markVideoAccepted();

    assert(gate.shouldEmitCompletion());
    assert(gate.completionPosition() == 1000ms);
}

void staleGenerationDoesNotCompleteSeek()
{
    media_sdk::SeekPrerollGate gate({
        .target = 1000ms,
        .generation = 2,
        .hasVideo = true,
        .hasAudio = false,
    });

    const auto decision = gate.inspectVideo(1200ms, 1);

    assert(decision.action == media_sdk::SeekPrerollAction::Stale);
    assert(!gate.shouldEmitCompletion());
}

void audioOnlyAcceptedFrameCompletesSeek()
{
    media_sdk::SeekPrerollGate gate({
        .target = 500ms,
        .generation = 4,
        .hasVideo = false,
        .hasAudio = true,
    });

    const auto decision = gate.inspectAudio(510ms, 4);
    assert(decision.action == media_sdk::SeekPrerollAction::Accept);
    gate.markAudioAccepted();

    assert(gate.shouldEmitCompletion());
}

} // namespace

int main()
{
    videoBeforeTargetIsDiscarded();
    videoAtTargetIsAcceptedAndCompletesVideoOnlySeek();
    staleGenerationDoesNotCompleteSeek();
    audioOnlyAcceptedFrameCompletesSeek();
}
```

- [ ] **Step 2: Register the test target**

Modify `sdk/media_core/CMakeLists.txt` under `if(BUILD_TESTING)`:

```cmake
add_executable(MediaSdkCoreSeekPrerollGateTest
    tests/tst_media_core_seek_preroll_gate.cpp
)
target_compile_features(MediaSdkCoreSeekPrerollGateTest PRIVATE cxx_std_20)
set_target_properties(MediaSdkCoreSeekPrerollGateTest PROPERTIES
    CXX_EXTENSIONS OFF
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_include_directories(MediaSdkCoreSeekPrerollGateTest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(MediaSdkCoreSeekPrerollGateTest PRIVATE media_sdk_core)
add_test(NAME media_sdk_core_seek_preroll_gate
    COMMAND $<TARGET_FILE:MediaSdkCoreSeekPrerollGateTest>
)
```

- [ ] **Step 3: Run and verify red**

Run:

```bash
cmake --build build --target MediaSdkCoreSeekPrerollGateTest --parallel
```

Expected: FAIL because `SeekPrerollGate.h` does not exist.

- [ ] **Step 4: Add minimal gate implementation**

Create `sdk/media_core/src/SeekPrerollGate.h`:

```cpp
#pragma once

#include <chrono>
#include <cstdint>

namespace media_sdk {

enum class SeekPrerollAction {
    Accept,
    Discard,
    Stale
};

struct SeekPrerollDecision {
    SeekPrerollAction action = SeekPrerollAction::Discard;
};

struct SeekPrerollGateConfig {
    std::chrono::microseconds target { 0 };
    std::uint64_t generation = 0;
    bool hasVideo = false;
    bool hasAudio = false;
};

class SeekPrerollGate
{
public:
    explicit SeekPrerollGate(SeekPrerollGateConfig config)
        : m_config(config)
    {
    }

    SeekPrerollDecision inspectVideo(std::chrono::microseconds pts,
                                     std::uint64_t generation) const
    {
        if (generation != m_config.generation)
            return { .action = SeekPrerollAction::Stale };
        return { .action = pts < m_config.target
            ? SeekPrerollAction::Discard
            : SeekPrerollAction::Accept };
    }

    SeekPrerollDecision inspectAudio(std::chrono::microseconds pts,
                                     std::uint64_t generation) const
    {
        if (generation != m_config.generation)
            return { .action = SeekPrerollAction::Stale };
        return { .action = pts < m_config.target
            ? SeekPrerollAction::Discard
            : SeekPrerollAction::Accept };
    }

    void markVideoAccepted()
    {
        m_videoReady = true;
    }

    void markAudioAccepted()
    {
        m_audioReady = true;
    }

    bool shouldEmitCompletion() const
    {
        if (m_completionSent)
            return false;
        if (m_config.hasVideo)
            return m_videoReady;
        if (m_config.hasAudio)
            return m_audioReady;
        return false;
    }

    void markCompletionSent()
    {
        m_completionSent = true;
    }

    std::chrono::microseconds completionPosition() const
    {
        return m_config.target;
    }

private:
    SeekPrerollGateConfig m_config;
    bool m_videoReady = false;
    bool m_audioReady = false;
    bool m_completionSent = false;
};

} // namespace media_sdk
```

- [ ] **Step 5: Run and verify green**

Run:

```bash
cmake --build build --target MediaSdkCoreSeekPrerollGateTest --parallel
ctest --test-dir build -R media_sdk_core_seek_preroll_gate --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add sdk/media_core/src/SeekPrerollGate.h \
        sdk/media_core/tests/tst_media_core_seek_preroll_gate.cpp \
        sdk/media_core/CMakeLists.txt
git commit -m "[功能修改] 增加精确seek预滚门控"
```

---

## Task 2: Add Sample-Aligned Audio Trimming

**Purpose:** Ensure accurate seek never sends audio samples before the target PTS.

**Files:**
- Create: `sdk/media_core/src/SeekAudioTrimmer.h`
- Create: `sdk/media_core/tests/tst_media_core_seek_audio_trimmer.cpp`
- Modify: `sdk/media_core/CMakeLists.txt`

- [ ] **Step 1: Write failing audio trim tests**

Create `sdk/media_core/tests/tst_media_core_seek_audio_trimmer.cpp`:

```cpp
#include "SeekAudioTrimmer.h"

#include "media_sdk/Frame.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace std::chrono_literals;

namespace {

std::vector<std::byte> bytesFromInt16(std::vector<std::int16_t> samples)
{
    std::vector<std::byte> bytes(samples.size() * sizeof(std::int16_t));
    std::memcpy(bytes.data(), samples.data(), bytes.size());
    return bytes;
}

void crossingInt16AudioIsTrimmedToTargetSample()
{
    auto frame = media_sdk::AudioFrame::fromOwnedSamples(
        media_sdk::AudioSampleFormat::Signed16Interleaved,
        1000,
        2,
        1000ms,
        bytesFromInt16({ 1, 2, 3, 4, 5, 6, 7, 8 }));

    const auto result = media_sdk::trimAudioFrameForSeek(frame, 1002ms);

    assert(result.status == media_sdk::SeekAudioTrimStatus::Trimmed);
    assert(result.frame.pts() == 1002ms);
    assert(result.frame.samples().size() == 4 * sizeof(std::int16_t));
}

void audioBeforeTargetIsDiscarded()
{
    auto frame = media_sdk::AudioFrame::fromOwnedSamples(
        media_sdk::AudioSampleFormat::Signed16Interleaved,
        1000,
        2,
        1000ms,
        bytesFromInt16({ 1, 2, 3, 4 }));

    const auto result = media_sdk::trimAudioFrameForSeek(frame, 1003ms);

    assert(result.status == media_sdk::SeekAudioTrimStatus::Discard);
}

void audioAfterTargetIsKept()
{
    auto frame = media_sdk::AudioFrame::fromOwnedSamples(
        media_sdk::AudioSampleFormat::Signed16Interleaved,
        1000,
        2,
        1005ms,
        bytesFromInt16({ 1, 2, 3, 4 }));

    const auto result = media_sdk::trimAudioFrameForSeek(frame, 1000ms);

    assert(result.status == media_sdk::SeekAudioTrimStatus::Keep);
    assert(result.frame.pts() == 1005ms);
}

void invalidFormatFails()
{
    auto frame = media_sdk::AudioFrame::fromOwnedSamples(
        media_sdk::AudioSampleFormat::Unknown,
        1000,
        2,
        1000ms,
        {});

    const auto result = media_sdk::trimAudioFrameForSeek(frame, 1000ms);

    assert(result.status == media_sdk::SeekAudioTrimStatus::Invalid);
}

} // namespace

int main()
{
    crossingInt16AudioIsTrimmedToTargetSample();
    audioBeforeTargetIsDiscarded();
    audioAfterTargetIsKept();
    invalidFormatFails();
}
```

- [ ] **Step 2: Register the test target**

Add to `sdk/media_core/CMakeLists.txt`:

```cmake
add_executable(MediaSdkCoreSeekAudioTrimmerTest
    tests/tst_media_core_seek_audio_trimmer.cpp
)
target_compile_features(MediaSdkCoreSeekAudioTrimmerTest PRIVATE cxx_std_20)
set_target_properties(MediaSdkCoreSeekAudioTrimmerTest PROPERTIES
    CXX_EXTENSIONS OFF
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_include_directories(MediaSdkCoreSeekAudioTrimmerTest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(MediaSdkCoreSeekAudioTrimmerTest PRIVATE media_sdk_core)
add_test(NAME media_sdk_core_seek_audio_trimmer
    COMMAND $<TARGET_FILE:MediaSdkCoreSeekAudioTrimmerTest>
)
```

- [ ] **Step 3: Run and verify red**

```bash
cmake --build build --target MediaSdkCoreSeekAudioTrimmerTest --parallel
```

Expected: FAIL because `SeekAudioTrimmer.h` does not exist.

- [ ] **Step 4: Implement minimal trimmer**

Create `sdk/media_core/src/SeekAudioTrimmer.h`:

```cpp
#pragma once

#include "media_sdk/Frame.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace media_sdk {

enum class SeekAudioTrimStatus {
    Keep,
    Trimmed,
    Discard,
    Invalid
};

struct SeekAudioTrimResult {
    SeekAudioTrimStatus status = SeekAudioTrimStatus::Invalid;
    AudioFrame frame;
};

inline std::size_t seekBytesPerSample(AudioSampleFormat format)
{
    switch (format) {
    case AudioSampleFormat::Float32Interleaved:
    case AudioSampleFormat::Signed32Interleaved:
        return 4;
    case AudioSampleFormat::Signed16Interleaved:
        return 2;
    case AudioSampleFormat::Unknown:
        return 0;
    }
    return 0;
}

inline SeekAudioTrimResult trimAudioFrameForSeek(
    const AudioFrame& frame,
    std::chrono::microseconds target)
{
    const auto bytesPerSample = seekBytesPerSample(frame.sampleFormat());
    if (bytesPerSample == 0 || frame.sampleRate() <= 0 || frame.channels() <= 0)
        return { .status = SeekAudioTrimStatus::Invalid };

    const auto bytesPerAudioFrame = bytesPerSample * static_cast<std::size_t>(frame.channels());
    if (bytesPerAudioFrame == 0 || frame.samples().size() % bytesPerAudioFrame != 0)
        return { .status = SeekAudioTrimStatus::Invalid };

    if (frame.pts() >= target)
        return { .status = SeekAudioTrimStatus::Keep, .frame = frame };

    const auto deltaUs = target - frame.pts();
    const auto trimFrames = static_cast<std::size_t>(
        (deltaUs.count() * static_cast<std::int64_t>(frame.sampleRate()) + 999999) / 1000000);
    const auto totalFrames = frame.samples().size() / bytesPerAudioFrame;
    if (trimFrames >= totalFrames)
        return { .status = SeekAudioTrimStatus::Discard };

    const auto trimBytes = trimFrames * bytesPerAudioFrame;
    std::vector<std::byte> samples(
        frame.samples().begin() + static_cast<std::ptrdiff_t>(trimBytes),
        frame.samples().end());
    const auto newPts = frame.pts() + std::chrono::microseconds {
        static_cast<std::int64_t>(trimFrames) * 1000000 / frame.sampleRate()
    };

    return {
        .status = SeekAudioTrimStatus::Trimmed,
        .frame = AudioFrame::fromOwnedSamples(
            frame.sampleFormat(),
            frame.sampleRate(),
            frame.channels(),
            newPts,
            std::move(samples)),
    };
}

} // namespace media_sdk
```

- [ ] **Step 5: Run and verify green**

```bash
cmake --build build --target MediaSdkCoreSeekAudioTrimmerTest --parallel
ctest --test-dir build -R media_sdk_core_seek_audio_trimmer --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add sdk/media_core/src/SeekAudioTrimmer.h \
        sdk/media_core/tests/tst_media_core_seek_audio_trimmer.cpp \
        sdk/media_core/CMakeLists.txt
git commit -m "[功能修改] 增加精确seek音频裁剪"
```

---

## Task 3: Delay Seek Completion Until Target-Side Media

**Purpose:** Stop reporting seek complete immediately after `av_seek_frame`; completion should mean the accurate gate reached playable target-side media.

**Files:**
- Modify: `sdk/media_core/src/DecodeWorker.h`
- Modify: `sdk/media_core/src/DecodeWorker.cpp`
- Modify: `sdk/media_core/tests/tst_media_core_playback_worker.cpp`

- [ ] **Step 1: Add failing playback worker test for delayed completion**

Modify `tst_media_core_playback_worker.cpp` with a test that blocks frame delivery, issues seek, and asserts no `SeekCompletedEvent` arrives before a target-side frame is accepted:

```cpp
void testSeekCompletionWaitsForAcceptedTargetFrame()
{
    const auto samplePath = writeTinyWav();
    RecordingSink sink;
    RecordingFrameSink frames;
    media_sdk::Player player({}, sink, frames);

    assert(player.open(samplePath).ok());
    assert(sink.waitFor(hasMediaInfo));

    frames.blockAudioFrames();
    assert(player.seek(100ms).ok());
    assert(!sink.waitFor([](const media_sdk::PlayerEvent& event) {
        return hasSeekCompletedAtOrAfter(event, 100ms);
    }, 100ms));

    frames.releaseAudioFrames();
    assert(sink.waitFor([](const media_sdk::PlayerEvent& event) {
        return hasSeekCompletedAtOrAfter(event, 100ms);
    }));

    player.stop();
    std::filesystem::remove(samplePath);
}
```

Add the function call in `main()`.

- [ ] **Step 2: Run and verify red**

```bash
cmake --build build --target MediaSdkCorePlaybackWorkerTest --parallel
ctest --test-dir build -R media_sdk_core_playback_worker --output-on-failure
```

Expected: FAIL because current `handleSeek()` emits completion immediately.

- [ ] **Step 3: Add pending seek state**

Modify `DecodeWorker.h`:

```cpp
#include "SeekPrerollGate.h"

#include <optional>

std::optional<SeekPrerollGate> m_seekGate;
std::optional<std::chrono::microseconds> m_pendingSeekTarget;
void beginAccurateSeek(std::chrono::milliseconds position);
void emitSeekCompletedIfReady();
```

- [ ] **Step 4: Start pending seek in handleSeek**

Modify `DecodeWorker::handleSeek()` so it no longer emits completion directly:

```cpp
++m_generation;
beginAccurateSeek(position);
return true;
```

Implement:

```cpp
void DecodeWorker::beginAccurateSeek(std::chrono::milliseconds position)
{
    const auto target = std::chrono::duration_cast<std::chrono::microseconds>(position);
    m_pendingSeekTarget = target;
    m_seekGate.emplace(SeekPrerollGateConfig {
        .target = target,
        .generation = m_generation,
        .hasVideo = m_media.videoStreamIndex >= 0 && m_media.videoCodecContext,
        .hasAudio = m_media.audioStreamIndex >= 0 && m_media.audioCodecContext,
    });
}
```

- [ ] **Step 5: Emit completion after accepted frame**

Add:

```cpp
void DecodeWorker::emitSeekCompletedIfReady()
{
    if (!m_seekGate || !m_seekGate->shouldEmitCompletion())
        return;

    const auto position = std::chrono::duration_cast<std::chrono::milliseconds>(
        m_seekGate->completionPosition());
    emitEvent(makeEvent(SeekCompletedEvent { position }));
    emitEvent(makeEvent(PositionChangedEvent { position }));
    m_seekGate->markCompletionSent();
    m_seekGate.reset();
    m_pendingSeekTarget.reset();
}
```

Call `emitSeekCompletedIfReady()` immediately after an audio or video push result is accepted/backpressured and the gate marks readiness.

- [ ] **Step 6: Run and verify green**

```bash
cmake --build build --target MediaSdkCorePlaybackWorkerTest --parallel
ctest --test-dir build -R media_sdk_core_playback_worker --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add sdk/media_core/src/DecodeWorker.h \
        sdk/media_core/src/DecodeWorker.cpp \
        sdk/media_core/tests/tst_media_core_playback_worker.cpp
git commit -m "[功能修改] 延后seek完成事件到目标帧"
```

---

## Task 4: Gate Video Before Frame Processing

**Purpose:** Avoid processing or pushing video frames before the seek target.

**Files:**
- Modify: `sdk/media_core/src/DecodeWorker.cpp`
- Modify: `sdk/media_core/tests/tst_media_core_playback_worker.cpp`

- [ ] **Step 1: Add failing test for paused video seek preroll**

Extend `RecordingFrameSink` to record first video PTS after seek if it does not already expose it. Add:

```cpp
void testPausedSeekDoesNotPrerollVideoBeforeTarget()
{
    const auto samplePath = writeAudioFirstVideoSample();
    RecordingSink sink;
    RecordingFrameSink frames;
    media_sdk::Player player({}, sink, frames);

    assert(player.open(samplePath).ok());
    assert(sink.waitFor(hasMediaInfo));

    assert(player.seek(100ms).ok());
    assert(frames.waitForVideoFrame(500ms));

    assert(frames.lastVideoPts() >= 100ms);

    player.stop();
    std::filesystem::remove(samplePath);
}
```

Create a focused synthetic media helper in `tst_media_core_playback_worker.cpp` named `writeLongGopVideoSample()`. It should write a short H.264 MP4 with at least one keyframe before 0ms and non-keyframes around 40ms, 80ms, 120ms, and 160ms. Use it in this test so the pre-fix behavior can expose a target-before preroll frame and the post-fix behavior must deliver a frame whose PTS is at least 100ms.

- [ ] **Step 2: Run and verify red**

```bash
cmake --build build --target MediaSdkCorePlaybackWorkerTest --parallel
ctest --test-dir build -R media_sdk_core_playback_worker --output-on-failure
```

Expected: FAIL because target-before preroll can currently be pushed.

- [ ] **Step 3: Inspect video PTS before processing**

In `DecodeWorker::decodePacket()` video branch, before `VideoFrameProcessor::process()`, add:

```cpp
if (m_seekGate) {
    const auto framePts = frame->pts == AV_NOPTS_VALUE
        ? std::chrono::microseconds { -1 }
        : std::chrono::microseconds { frame->pts };
    const auto decision = frame->pts == AV_NOPTS_VALUE
        ? SeekPrerollDecision { .action = SeekPrerollAction::Discard }
        : m_seekGate->inspectVideo(framePts, m_generation);
    if (decision.action == SeekPrerollAction::Discard)
        return StreamDecoder::FrameHandlerStatus::Continue;
    if (decision.action == SeekPrerollAction::Stale)
        return StreamDecoder::FrameHandlerStatus::Continue;
}
```

- [ ] **Step 4: Mark video ready only after delivered push**

After `auto pushResult = m_frames.pushVideo(...)`:

```cpp
if (m_seekGate && isDeliveredFramePush(pushResult)) {
    m_seekGate->markVideoAccepted();
    emitSeekCompletedIfReady();
}
```

- [ ] **Step 5: Run and verify green**

```bash
cmake --build build --target MediaSdkCorePlaybackWorkerTest --parallel
ctest --test-dir build -R media_sdk_core_playback_worker --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add sdk/media_core/src/DecodeWorker.cpp \
        sdk/media_core/tests/tst_media_core_playback_worker.cpp
git commit -m "[功能修改] 精确seek丢弃目标前视频帧"
```

---

## Task 5: Gate and Trim Audio Before Push

**Purpose:** Prevent target-before audio from entering runtime and trim crossing audio frames.

**Files:**
- Modify: `sdk/media_core/src/DecodeWorker.cpp`
- Modify: `sdk/media_core/tests/tst_media_core_playback_worker.cpp`

- [ ] **Step 1: Add failing integration test for audio trim**

Add a test using `writeTinyWav()` or a new fixture with predictable sample rate:

```cpp
void testSeekDoesNotPushAudioBeforeTarget()
{
    const auto samplePath = writeTinyWav();
    RecordingSink sink;
    RecordingFrameSink frames;
    media_sdk::Player player({}, sink, frames);

    assert(player.open(samplePath).ok());
    assert(sink.waitFor(hasMediaInfo));

    assert(player.seek(100ms).ok());
    assert(frames.waitForAudioFrame());

    assert(frames.lastAudioPts() >= 100ms);

    player.stop();
    std::filesystem::remove(samplePath);
}
```

Add the function call in `main()`.

- [ ] **Step 2: Run and verify red**

```bash
cmake --build build --target MediaSdkCorePlaybackWorkerTest --parallel
ctest --test-dir build -R media_sdk_core_playback_worker --output-on-failure
```

Expected: FAIL on current keyframe-backward behavior or pass only if the WAV fixture is already exact. If it passes due fixture limitations, keep the helper unit tests from Task 2 as the red proof and add a deterministic audio fixture with packet PTS before target.

- [ ] **Step 3: Apply audio trim in decode callback**

In audio branch after `AudioFrame audioFrame = makeAudioFrame(std::move(frame));`:

```cpp
if (m_seekGate && m_pendingSeekTarget.has_value()) {
    auto trimResult = trimAudioFrameForSeek(audioFrame, *m_pendingSeekTarget);
    if (trimResult.status == SeekAudioTrimStatus::Discard)
        return StreamDecoder::FrameHandlerStatus::Continue;
    if (trimResult.status == SeekAudioTrimStatus::Invalid) {
        emitError(makeError(MediaErrorCode::DecodeFailed,
                            "Failed to trim audio for accurate seek"));
        return StreamDecoder::FrameHandlerStatus::Reject;
    }
    if (trimResult.status == SeekAudioTrimStatus::Trimmed ||
        trimResult.status == SeekAudioTrimStatus::Keep) {
        audioFrame = std::move(trimResult.frame);
    }
}
```

- [ ] **Step 4: Mark audio ready only after delivered push**

After `auto pushResult = m_frames.pushAudio(...)`:

```cpp
if (m_seekGate && isDeliveredFramePush(pushResult)) {
    m_seekGate->markAudioAccepted();
    emitSeekCompletedIfReady();
}
```

- [ ] **Step 5: Run and verify green**

```bash
cmake --build build --target MediaSdkCorePlaybackWorkerTest --parallel
ctest --test-dir build -R media_sdk_core_playback_worker --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add sdk/media_core/src/DecodeWorker.cpp \
        sdk/media_core/tests/tst_media_core_playback_worker.cpp
git commit -m "[功能修改] 精确seek裁剪目标前音频"
```

---

## Task 6: Preserve Burst Seek and Stale Generation Semantics

**Purpose:** Ensure accurate seek does not regress coalesced seek, stale frame suppression, or pending seek mapping.

**Files:**
- Modify: `sdk/media_core/tests/tst_media_core_playback_worker.cpp`
- Modify: `sdk/media_playback_session/tests/tst_playback_session_contract.cpp`

- [ ] **Step 1: Extend burst seek test expectations**

In `testBurstSeekCoalescesQueuedRequestsBeforeDecodeResumes()`, keep:

```cpp
assert(seekCompletedCount == 1);
assert(lastSeekPosition == 200ms);
```

Add:

```cpp
assert(frames.lastAudioPts() >= 200ms);
```

- [ ] **Step 2: Add session contract assertion**

In `seekCallsRuntimeSeekBeforeCoreSeek()` or a new session test, assert delayed seek completion still maps to runtime completion:

```cpp
assert(session->seek(750ms).ok());
context.core->emitSeekCompleted(coreTimeline(10, 4), 750ms);
assert(context.runtime->completeSeekCount == 1);
assert(events.events.size() == 2);
```

This existing behavior must remain green.

- [ ] **Step 3: Run focused tests**

```bash
cmake --build build --target MediaSdkCorePlaybackWorkerTest --parallel
cmake --build build --target MediaSdkPlaybackSessionContractTest --parallel
ctest --test-dir build -R "media_sdk_core_playback_worker|media_sdk_playback_session_contract" --output-on-failure
```

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add sdk/media_core/tests/tst_media_core_playback_worker.cpp \
        sdk/media_playback_session/tests/tst_playback_session_contract.cpp
git commit -m "[测试] 覆盖精确seek代际与会话映射"
```

---

## Task 7: Add EOF and Bounded Preroll Behavior

**Purpose:** Prevent accurate seek from hanging near EOF or on files with sparse/invalid timestamps.

**Files:**
- Modify: `sdk/media_core/src/SeekPrerollGate.h`
- Modify: `sdk/media_core/src/DecodeWorker.cpp`
- Modify: `sdk/media_core/tests/tst_media_core_seek_preroll_gate.cpp`
- Modify: `sdk/media_core/tests/tst_media_core_playback_worker.cpp`

- [ ] **Step 1: Add gate bound tests**

Extend `tst_media_core_seek_preroll_gate.cpp`:

```cpp
void discardLimitAllowsCompletionFallback()
{
    media_sdk::SeekPrerollGate gate({
        .target = 5000ms,
        .generation = 3,
        .hasVideo = true,
        .hasAudio = false,
        .maxDiscardedVideoFrames = 2,
    });

    assert(gate.inspectVideo(1000ms, 3).action == media_sdk::SeekPrerollAction::Discard);
    gate.markVideoDiscarded();
    assert(gate.inspectVideo(2000ms, 3).action == media_sdk::SeekPrerollAction::Discard);
    gate.markVideoDiscarded();

    assert(gate.discardLimitReached());
}
```

- [ ] **Step 2: Add config fields**

Extend `SeekPrerollGateConfig`:

```cpp
int maxDiscardedVideoFrames = 300;
int maxDiscardedAudioFrames = 1000;
```

Add counters and `discardLimitReached()`.

- [ ] **Step 3: Handle EOF after pending seek**

In `decodeUntilBlocked()` and `decodeSeekPreroll()`, when `av_read_frame` returns `AVERROR_EOF` and `m_seekGate` exists:

```cpp
emitSeekCompletedIfReady();
if (m_seekGate) {
    emitEvent(makeEvent(SeekCompletedEvent {
        std::chrono::duration_cast<std::chrono::milliseconds>(
            m_seekGate->completionPosition())
    }));
    m_seekGate.reset();
    m_pendingSeekTarget.reset();
}
```

Then continue existing EOF behavior.

- [ ] **Step 4: Run focused tests**

```bash
cmake --build build --target MediaSdkCoreSeekPrerollGateTest --parallel
cmake --build build --target MediaSdkCorePlaybackWorkerTest --parallel
ctest --test-dir build -R "media_sdk_core_seek_preroll_gate|media_sdk_core_playback_worker" --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add sdk/media_core/src/SeekPrerollGate.h \
        sdk/media_core/src/DecodeWorker.cpp \
        sdk/media_core/tests/tst_media_core_seek_preroll_gate.cpp \
        sdk/media_core/tests/tst_media_core_playback_worker.cpp
git commit -m "[稳定性修复] 限制精确seek预滚边界"
```

---

## Task 8: Full SDK Verification and Documentation Check

**Purpose:** Confirm accurate seek integrates cleanly with core, session, runtime, and PlayPlugin adapter assumptions.

**Files:**
- Modify only if verification exposes a focused regression.

- [ ] **Step 1: Run SDK-focused tests**

```bash
ctest --test-dir build -R "media_sdk_core|media_sdk_playback_session|media_sdk_playback_runtime" --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 2: Run PlayPlugin adapter tests**

```bash
ctest --test-dir build -R "PlayPluginSdkPlaybackAdapterTest|playplugin" --output-on-failure
```

Expected: adapter tests pass. If a pending seek mapping test fails, adjust only the adapter/session mapping test seam, not the core accurate seek gate.

- [ ] **Step 3: Run full build**

```bash
cmake --build build --parallel
```

Expected: build exits 0. Existing duplicate static library linker warnings do not block this task.

- [ ] **Step 4: Run full CTest**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all configured tests pass.

- [ ] **Step 5: Commit final integration adjustments**

If no files changed in this task, do not create an empty commit. If focused fixes were required:

```bash
git status --short
git add sdk/media_core/src/DecodeWorker.cpp \
        sdk/media_core/src/DecodeWorker.h \
        sdk/media_core/src/SeekPrerollGate.h \
        sdk/media_core/src/SeekAudioTrimmer.h \
        sdk/media_core/tests/tst_media_core_playback_worker.cpp \
        sdk/media_playback_session/tests/tst_playback_session_contract.cpp \
        sdk/media_playback_runtime/tests/tst_runtime_player_mock.cpp
git commit -m "[稳定性修复] 完成精确seek集成验证"
```

## Self-Review Checklist

- Every target-before frame is filtered before `IDecodeFrameSink`.
- Audio trimming preserves complete interleaved sample frames.
- `SeekCompletedEvent` is no longer emitted directly from `handleSeek()`.
- Paused seek preroll cannot show target-before video.
- Burst seek still completes only the last coalesced target.
- Runtime queue and audio clock never receive target-before audio.
- Public `Player::seek(std::chrono::milliseconds)` remains source-compatible.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-05-media-sdk-accurate-seek.md`. Two execution options:

1. Subagent-Driven (recommended) - dispatch a fresh subagent per task, review between tasks, fast iteration.
2. Inline Execution - execute tasks in this session using executing-plans, batch execution with checkpoints.
