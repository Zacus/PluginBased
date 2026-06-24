# SDK Playback Runtime macOS Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a macOS-first SDK playback runtime that owns audio output, A/V queues, master clock sync, video scheduling, native fallback, and diagnostics, while PlayPlugin becomes a Qt presentation layer.

**Architecture:** Add `media_sdk_playback_runtime` as a Qt-free C++20 SDK layer above `media_sdk_core`; inject `IAudioOutput` and `IVideoPresenter` so runtime owns policy but not platform UI. Add a separate macOS CoreAudio target and a PlayPlugin Qt RHI presenter target path so platform details stay outside the runtime core.

**Tech Stack:** C++20, CMake, FFmpeg-backed `media_sdk_core`, macOS CoreAudio/AudioToolbox, Qt 6 RHI/Scene Graph inside PlayPlugin, Python architecture checks, CTest.

---

## Execution Rules

- Execute one task at a time.
- Each task must end with one focused commit.
- After each commit, stop and ask the user to confirm before starting the next task.
- Do not add untracked archives such as `plugins/PlayPlugin/PlayPlugin.zip` or `sdk/media_core.zip`.
- Runtime SDK code must stay Qt-free: no `QObject`, `QQuickItem`, `QSGNode`, `QRhi`, or `QAudioSink` in `sdk/media_playback_runtime`.
- Runtime SDK must not include `CoreAudioAudioOutput` concrete headers; it only depends on `IAudioOutput`.
- Ownership and shutdown must be explicit: queues wake on abort, callbacks are generation-filtered, and pending present handles are cleared before native resources are destroyed.
- The old PlayPlugin playback chain stays available until the new runtime is manually verified and the user approves changing defaults.

## File Map

### New SDK Runtime Target

- Create `sdk/media_playback_runtime/CMakeLists.txt`: target `media_sdk_playback_runtime`, alias `media_sdk::playback_runtime`, C++20, Qt automoc disabled.
- Create `sdk/media_playback_runtime/include/media_sdk/runtime/AudioOutput.h`: pure C++ audio output and clock contracts.
- Create `sdk/media_playback_runtime/include/media_sdk/runtime/VideoPresenter.h`: pure C++ video presenter, capability, completion, and failure contracts.
- Create `sdk/media_playback_runtime/include/media_sdk/runtime/RuntimeTypes.h`: session/generation ids, output policy, frame wrappers, diagnostics.
- Create `sdk/media_playback_runtime/include/media_sdk/runtime/RuntimePlayer.h`: public runtime facade after state machine and scheduler exist.
- Create `sdk/media_playback_runtime/src/RuntimeFrameQueue.h`: bounded blocking queue with generation, EOF, abort, and drain semantics.
- Create `sdk/media_playback_runtime/src/MasterClock.h` and `.cpp`: audio-clock-first master clock with video-only fallback.
- Create `sdk/media_playback_runtime/src/AvSyncScheduler.h` and `.cpp`: wait/drop/render decisions.
- Create `sdk/media_playback_runtime/src/PresentTracker.h` and `.cpp`: present id, pending depth, async completion filtering.
- Create `sdk/media_playback_runtime/src/NativeFallbackController.h` and `.cpp`: full fallback state transition.
- Create `sdk/media_playback_runtime/src/RuntimePlayer.cpp`: event ingest and runtime orchestration once lower layers are tested.
- Create focused tests under `sdk/media_playback_runtime/tests/`.

### New macOS Audio Target

- Create `sdk/media_platform_audio_macos/CMakeLists.txt`: target `media_sdk_platform_audio_macos`, alias `media_sdk::platform_audio_macos`, only built on Apple.
- Create `sdk/media_platform_audio_macos/include/media_sdk/platform/macos/CoreAudioAudioOutput.h`: concrete `IAudioOutput` implementation.
- Create `sdk/media_platform_audio_macos/src/CoreAudioAudioOutput.cpp`: CoreAudio device lifecycle, ring buffer, clock snapshot, pause/resume/flush/close.

### PlayPlugin Integration

- Create `plugins/PlayPlugin/src/playback/QtRhiVideoPresenter.h` and `.cpp`: `IVideoPresenter` implementation that marshals into Qt GUI/Scene Graph.
- Modify `plugins/PlayPlugin/CMakeLists.txt`: link new runtime target and add presenter files.
- Modify existing PlayPlugin playback composition files after identifying the current owner of `PlayerEngine`, `PlaybackDataBridge`, `FFmpegSurface`, and audio renderer.
- Keep old `QtPlaybackAdapter + PlaybackDataBridge + AudioRenderer + VideoRenderer + FrameQueue` path available behind a runtime mode switch.

### Tests And Architecture Checks

- Create `tests/media_sdk_playback_runtime_architecture_checks.py`: validates runtime Qt-free boundary, target C++20, interface contracts, and old/new chain mutual exclusion markers.
- Modify root `CMakeLists.txt`: `add_subdirectory(sdk/media_playback_runtime)`, optional Apple audio subdirectory, and register the new architecture check.
- Add CTest executables under `sdk/media_playback_runtime/tests/` for queues, scheduler, present tracker, fallback state machine, and runtime player with mock outputs.

## Task 1: Runtime Target Skeleton And Architecture Gate

**Files:**
- Create: `sdk/media_playback_runtime/CMakeLists.txt`
- Create: `sdk/media_playback_runtime/include/media_sdk/runtime/PlaybackRuntimeVersion.h`
- Create: `sdk/media_playback_runtime/src/PlaybackRuntimeVersion.cpp`
- Create: `tests/media_sdk_playback_runtime_architecture_checks.py`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the architecture check first**

Create `tests/media_sdk_playback_runtime_architecture_checks.py` with checks equivalent to this contract:

```python
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "sdk" / "media_playback_runtime"
ROOT_CMAKE = ROOT / "CMakeLists.txt"

FORBIDDEN_RUNTIME_TOKENS = (
    "QObject", "QQuickItem", "QSGNode", "QRhi", "QAudioSink",
    "#include <QObject", "#include <QQuickItem", "#include <QSGNode",
    "#include <QRhi", "#include <QAudio",
    "CoreAudioAudioOutput",
)

def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")

def assert_contains(text: str, token: str, source: Path) -> None:
    if token not in text:
        raise AssertionError(f"{source} must contain {token!r}")

def main() -> None:
    cmake = read(ROOT_CMAKE)
    assert_contains(cmake, "add_subdirectory(sdk/media_playback_runtime)", ROOT_CMAKE)
    assert_contains(cmake, "media_sdk_playback_runtime_architecture_checks", ROOT_CMAKE)

    runtime_cmake = read(RUNTIME / "CMakeLists.txt")
    assert_contains(runtime_cmake, "add_library(media_sdk_playback_runtime STATIC", RUNTIME / "CMakeLists.txt")
    assert_contains(runtime_cmake, "add_library(media_sdk::playback_runtime ALIAS media_sdk_playback_runtime)", RUNTIME / "CMakeLists.txt")
    assert_contains(runtime_cmake, "target_compile_features(media_sdk_playback_runtime PUBLIC cxx_std_20)", RUNTIME / "CMakeLists.txt")
    assert_contains(runtime_cmake, "AUTOMOC OFF", RUNTIME / "CMakeLists.txt")
    assert_contains(runtime_cmake, "media_sdk::core", RUNTIME / "CMakeLists.txt")

    scanned = []
    for suffix in ("*.h", "*.cpp"):
        scanned.extend(RUNTIME.rglob(suffix))
    if not scanned:
        raise AssertionError("runtime target must contain source/header files")
    for path in scanned:
        text = read(path)
        for token in FORBIDDEN_RUNTIME_TOKENS:
            if token in text:
                raise AssertionError(f"{path} contains forbidden runtime dependency token {token!r}")

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run the new check and confirm it fails before implementation**

Run: `python3 tests/media_sdk_playback_runtime_architecture_checks.py`

Expected: FAIL because `sdk/media_playback_runtime/CMakeLists.txt` does not exist or root `CMakeLists.txt` does not register it.

- [ ] **Step 3: Add the minimal runtime target**

Create `sdk/media_playback_runtime/CMakeLists.txt`:

```cmake
add_library(media_sdk_playback_runtime STATIC
    src/PlaybackRuntimeVersion.cpp
)

add_library(media_sdk::playback_runtime ALIAS media_sdk_playback_runtime)

target_compile_features(media_sdk_playback_runtime PUBLIC cxx_std_20)
set_target_properties(media_sdk_playback_runtime PROPERTIES
    CXX_EXTENSIONS OFF
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

target_include_directories(media_sdk_playback_runtime PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(media_sdk_playback_runtime PUBLIC
    media_sdk::core
)
```

Create `sdk/media_playback_runtime/include/media_sdk/runtime/PlaybackRuntimeVersion.h`:

```cpp
#pragma once

#include <string_view>

namespace media_sdk::runtime {

std::string_view playbackRuntimeVersion() noexcept;

} // namespace media_sdk::runtime
```

Create `sdk/media_playback_runtime/src/PlaybackRuntimeVersion.cpp`:

```cpp
#include "media_sdk/runtime/PlaybackRuntimeVersion.h"

namespace media_sdk::runtime {

std::string_view playbackRuntimeVersion() noexcept
{
    return "0.1.0";
}

} // namespace media_sdk::runtime
```

Modify root `CMakeLists.txt` near `add_subdirectory(sdk/media_core)`:

```cmake
add_subdirectory(sdk/media_core)
add_subdirectory(sdk/media_playback_runtime)
```

Register the architecture check in the `if(BUILD_TESTING)` block:

```cmake
add_test(NAME media_sdk_playback_runtime_architecture_checks
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/media_sdk_playback_runtime_architecture_checks.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
```

- [ ] **Step 4: Verify the architecture gate passes**

Run: `python3 tests/media_sdk_playback_runtime_architecture_checks.py`

Expected: PASS with no output.

- [ ] **Step 5: Configure and build the new target**

Run: `cmake --build build --target media_sdk_playback_runtime --parallel`

Expected: target builds successfully. If `build/` has not been configured, run the existing project configure command before building.

- [ ] **Step 6: Commit Task 1**

```bash
git add CMakeLists.txt sdk/media_playback_runtime tests/media_sdk_playback_runtime_architecture_checks.py
git commit -m "[功能新增] 增加SDK播放运行时骨架"
```

Stop after the commit and ask the user to confirm Task 2.

## Task 2: Public Runtime Interface Contracts

**Files:**
- Create: `sdk/media_playback_runtime/include/media_sdk/runtime/AudioOutput.h`
- Create: `sdk/media_playback_runtime/include/media_sdk/runtime/VideoPresenter.h`
- Create: `sdk/media_playback_runtime/include/media_sdk/runtime/RuntimeTypes.h`
- Create: `sdk/media_playback_runtime/tests/tst_runtime_interface_contract.cpp`
- Modify: `sdk/media_playback_runtime/CMakeLists.txt`
- Modify: `tests/media_sdk_playback_runtime_architecture_checks.py`

- [ ] **Step 1: Add a compiling interface contract test**

Create `sdk/media_playback_runtime/tests/tst_runtime_interface_contract.cpp`:

```cpp
#include "media_sdk/runtime/AudioOutput.h"
#include "media_sdk/runtime/RuntimeTypes.h"
#include "media_sdk/runtime/VideoPresenter.h"

#include <cassert>
#include <chrono>
#include <memory>
#include <vector>

using namespace std::chrono_literals;

namespace {

class MockAudioOutput final : public media_sdk::runtime::IAudioOutput {
public:
    media_sdk::Result<void> open(const media_sdk::runtime::AudioFormat& format) override
    {
        m_format = format;
        return {};
    }

    media_sdk::Result<void> write(media_sdk::runtime::AudioBufferView buffer) override
    {
        m_written += buffer.bytes.size();
        return {};
    }

    media_sdk::runtime::ClockSnapshot clock() const override
    {
        return media_sdk::runtime::ClockSnapshot {
            .position = 42ms,
            .hardwareLatency = 3ms,
            .queuedDuration = 8ms,
            .generation = 7,
            .valid = true,
            .paused = false,
        };
    }

    void pause() override { m_paused = true; }
    void resume() override { m_paused = false; }
    void flush() override { ++m_flushCount; }
    void close() override { m_closed = true; }

    media_sdk::runtime::AudioFormat m_format {};
    std::size_t m_written = 0;
    int m_flushCount = 0;
    bool m_paused = false;
    bool m_closed = false;
};

class MockPresenter final : public media_sdk::runtime::IVideoPresenter {
public:
    media_sdk::runtime::VideoPresenterCapabilities capabilities() const override
    {
        return {
            .supportsVideoToolboxPixelBuffer = true,
            .supportsCpuYuv = true,
            .asyncPresent = true,
        };
    }

    void setEvents(media_sdk::runtime::IVideoPresenterEvents* events) override
    {
        m_events = events;
    }

    media_sdk::runtime::PresentResult present(
        media_sdk::VideoFrame frame,
        media_sdk::runtime::PresentTiming timing) override
    {
        m_lastPts = timing.pts;
        m_lastFrame = std::move(frame);
        return {
            .id = ++m_nextId,
            .status = media_sdk::runtime::PresentStatus::Queued,
        };
    }

    void clear() override
    {
        m_cleared = true;
    }

    media_sdk::runtime::IVideoPresenterEvents* m_events = nullptr;
    media_sdk::VideoFrame m_lastFrame {};
    std::chrono::microseconds m_lastPts { 0 };
    media_sdk::runtime::PresentId m_nextId = 0;
    bool m_cleared = false;
};

} // namespace

int main()
{
    MockAudioOutput audio;
    const auto openResult = audio.open({ 48000, 2, media_sdk::runtime::AudioSampleFormat::Float32Planar });
    assert(openResult.hasValue());
    assert(audio.clock().valid);
    assert(audio.clock().generation == 7);

    std::vector<std::byte> bytes(128);
    const auto writeResult = audio.write({ bytes.data(), bytes.size(), 1000us, 7 });
    assert(writeResult.hasValue());
    assert(audio.m_written == bytes.size());

    MockPresenter presenter;
    assert(presenter.capabilities().supportsVideoToolboxPixelBuffer);
    const auto result = presenter.present({}, { 1000us, 995us, 5us });
    assert(result.id == 1);
    assert(result.status == media_sdk::runtime::PresentStatus::Queued);

    media_sdk::runtime::RuntimeVideoFrame runtimeFrame;
    runtimeFrame.sessionId = 11;
    runtimeFrame.generation = 22;
    assert(runtimeFrame.sessionId == 11);
    assert(runtimeFrame.generation == 22);
}
```

- [ ] **Step 2: Register the test and confirm it fails because headers are missing**

Modify `sdk/media_playback_runtime/CMakeLists.txt` inside `if(BUILD_TESTING)`:

```cmake
if(BUILD_TESTING)
    add_executable(MediaSdkPlaybackRuntimeInterfaceContractTest
        tests/tst_runtime_interface_contract.cpp
    )
    target_compile_features(MediaSdkPlaybackRuntimeInterfaceContractTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkPlaybackRuntimeInterfaceContractTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_link_libraries(MediaSdkPlaybackRuntimeInterfaceContractTest PRIVATE
        media_sdk_playback_runtime
    )
    add_test(NAME media_sdk_playback_runtime_interface_contract
        COMMAND $<TARGET_FILE:MediaSdkPlaybackRuntimeInterfaceContractTest>
    )
endif()
```

Run: `cmake --build build --target MediaSdkPlaybackRuntimeInterfaceContractTest --parallel`

Expected: FAIL because `AudioOutput.h`, `RuntimeTypes.h`, and `VideoPresenter.h` do not exist.

- [ ] **Step 3: Add public contracts**

Create `sdk/media_playback_runtime/include/media_sdk/runtime/RuntimeTypes.h`:

```cpp
#pragma once

#include "media_sdk/Frame.h"

#include <chrono>
#include <cstdint>

namespace media_sdk::runtime {

using SessionId = std::uint64_t;
using Generation = std::uint64_t;

enum class VideoOutputPolicy {
    PreferNative,
    CpuOnly,
    RequireNative
};

struct RuntimeAudioFrame {
    AudioFrame frame;
    SessionId sessionId = 0;
    Generation generation = 0;
    bool endOfStream = false;
};

struct RuntimeVideoFrame {
    VideoFrame frame;
    SessionId sessionId = 0;
    Generation generation = 0;
    bool endOfStream = false;
};

struct RuntimeDiagnostics {
    std::uint64_t nativeDecoded = 0;
    std::uint64_t nativeAccepted = 0;
    std::uint64_t nativePresented = 0;
    std::uint64_t nativeFallbacks = 0;
    std::uint64_t nativeTextureCreated = 0;
    std::uint64_t nativeTextureFailed = 0;
    std::uint64_t nativeTextureDrawn = 0;
    std::uint64_t cpuDecoded = 0;
    std::uint64_t cpuPresented = 0;
    std::uint64_t cpuCopied = 0;
    std::uint64_t cpuTransferred = 0;
    std::uint64_t cpuMemcpy = 0;
    std::uint64_t hardwareTransfers = 0;
    std::uint64_t audioQueued = 0;
    std::uint64_t audioWritten = 0;
    std::uint64_t videoQueued = 0;
    std::uint64_t videoWaited = 0;
    std::uint64_t videoDroppedLate = 0;
    std::uint64_t videoPresented = 0;
    std::uint64_t eofAccepted = 0;
    std::uint64_t eofPresented = 0;
    std::uint64_t queueAbortCount = 0;
};

} // namespace media_sdk::runtime
```

Create `sdk/media_playback_runtime/include/media_sdk/runtime/AudioOutput.h`:

```cpp
#pragma once

#include "media_sdk/Result.h"
#include "media_sdk/runtime/RuntimeTypes.h"

#include <chrono>
#include <cstddef>
#include <span>

namespace media_sdk::runtime {

enum class AudioSampleFormat {
    Unknown,
    UInt8,
    Int16,
    Int32,
    Float32,
    Float32Planar
};

struct AudioFormat {
    int sampleRate = 0;
    int channels = 0;
    AudioSampleFormat sampleFormat = AudioSampleFormat::Unknown;
};

struct AudioBufferView {
    std::span<const std::byte> bytes;
    std::chrono::microseconds pts { 0 };
    Generation generation = 0;
};

struct ClockSnapshot {
    std::chrono::microseconds position { 0 };
    std::chrono::microseconds hardwareLatency { 0 };
    std::chrono::microseconds queuedDuration { 0 };
    Generation generation = 0;
    bool valid = false;
    bool paused = false;
};

class IAudioOutput {
public:
    virtual ~IAudioOutput() = default;

    virtual Result<void> open(const AudioFormat& format) = 0;
    virtual Result<void> write(AudioBufferView buffer) = 0;
    virtual ClockSnapshot clock() const = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void flush() = 0;
    virtual void close() = 0;
};

} // namespace media_sdk::runtime
```

Create `sdk/media_playback_runtime/include/media_sdk/runtime/VideoPresenter.h`:

```cpp
#pragma once

#include "media_sdk/Frame.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace media_sdk::runtime {

using PresentId = std::uint64_t;

struct VideoPresenterCapabilities {
    bool supportsVideoToolboxPixelBuffer = false;
    bool supportsCpuYuv = true;
    bool asyncPresent = true;
    std::uint32_t maxPendingFrames = 1;
};

enum class PresentStatus {
    Presented,
    Queued,
    UnsupportedNativeHandle,
    DeviceLost,
    Failed
};

struct PresentTiming {
    std::chrono::microseconds pts { 0 };
    std::chrono::microseconds clock { 0 };
    std::chrono::microseconds lateness { 0 };
};

struct PresentResult {
    PresentId id = 0;
    PresentStatus status = PresentStatus::Failed;
};

struct PresentCompletion {
    PresentId id = 0;
    PresentStatus status = PresentStatus::Failed;
    std::string detail;
};

class IVideoPresenterEvents {
public:
    virtual ~IVideoPresenterEvents() = default;
    virtual void onPresentComplete(PresentCompletion completion) = 0;
};

class IVideoPresenter {
public:
    virtual ~IVideoPresenter() = default;

    virtual VideoPresenterCapabilities capabilities() const = 0;
    virtual void setEvents(IVideoPresenterEvents* events) = 0;
    virtual PresentResult present(VideoFrame frame, PresentTiming timing) = 0;
    virtual void clear() = 0;
};

} // namespace media_sdk::runtime
```

- [ ] **Step 4: Strengthen the architecture check**

Extend `tests/media_sdk_playback_runtime_architecture_checks.py` to assert:

```python
for header_name in ("AudioOutput.h", "VideoPresenter.h", "RuntimeTypes.h"):
    header = RUNTIME / "include" / "media_sdk" / "runtime" / header_name
    if not header.exists():
        raise AssertionError(f"{header} must exist")

video_presenter = read(RUNTIME / "include" / "media_sdk" / "runtime" / "VideoPresenter.h")
for token in ("IVideoPresenterEvents", "PresentCompletion", "onPresentComplete", "PresentId"):
    assert_contains(video_presenter, token, RUNTIME / "include" / "media_sdk" / "runtime" / "VideoPresenter.h")
```

- [ ] **Step 5: Verify**

Run:

```bash
python3 tests/media_sdk_playback_runtime_architecture_checks.py
cmake --build build --target MediaSdkPlaybackRuntimeInterfaceContractTest --parallel
ctest --test-dir build -R media_sdk_playback_runtime_interface_contract --output-on-failure
```

Expected: all pass.

- [ ] **Step 6: Commit Task 2**

```bash
git add sdk/media_playback_runtime tests/media_sdk_playback_runtime_architecture_checks.py
git commit -m "[功能新增] 定义SDK播放运行时接口"
```

Stop after the commit and ask the user to confirm Task 3.

## Task 3: Runtime Frame Queues, Generation Filter, And EOF Drain Contract

**Files:**
- Create: `sdk/media_playback_runtime/src/RuntimeFrameQueue.h`
- Create: `sdk/media_playback_runtime/tests/tst_runtime_frame_queue.cpp`
- Modify: `sdk/media_playback_runtime/CMakeLists.txt`

- [ ] **Step 1: Add queue behavior tests**

Create `sdk/media_playback_runtime/tests/tst_runtime_frame_queue.cpp` with tests for:

```cpp
int main()
{
    testPushPopPreservesAcceptedOrder();
    testQueueRejectsOldGeneration();
    testEofIsDeliveredAfterAcceptedFrames();
    testAbortWakesWaitersAndClearsPendingFrames();
    testFinishDoesNotAcceptMoreFrames();
}
```

The assertions must cover these exact behaviors:

- A bounded queue accepts frames for the active `(sessionId, generation)`.
- A frame from an older generation is rejected without blocking.
- EOF marker is popped only after already accepted frames in the same generation.
- `abort()` clears frames, increments abort diagnostics, and wakes blocking pop.
- `finish()` marks the queue closed for the current generation without invalidating newer generations.

- [ ] **Step 2: Run the test target and confirm it fails**

Register `MediaSdkPlaybackRuntimeFrameQueueTest` in `sdk/media_playback_runtime/CMakeLists.txt`.

Run: `cmake --build build --target MediaSdkPlaybackRuntimeFrameQueueTest --parallel`

Expected: FAIL because `RuntimeFrameQueue.h` does not exist.

- [ ] **Step 3: Implement `RuntimeFrameQueue`**

Implement a header-only template with this public shape:

```cpp
template <typename Frame>
class RuntimeFrameQueue {
public:
    enum class PushResult {
        Accepted,
        RejectedGeneration,
        Closed,
        Aborted
    };

    enum class PopResult {
        Frame,
        EndOfStream,
        Aborted,
        Closed
    };

    explicit RuntimeFrameQueue(std::size_t capacity);

    void reset(SessionId sessionId, Generation generation);
    PushResult push(Frame frame);
    PushResult pushEndOfStream(SessionId sessionId, Generation generation);
    PopResult waitPop(Frame& frame);
    void abort();
    void finish();
    std::size_t size() const;
    Generation generation() const;
};
```

Implementation rules:

- Protect all mutable state with `std::mutex`.
- Use `std::condition_variable` for producer and consumer wakeup.
- Never block on stale generation input.
- `abort()` must call `notify_all()` on both condition variables.
- Queue stores values so `VideoFrame::storage` lifetime is preserved while queued.
- No Qt types and no FFmpeg `AVFrame*` queue types.

- [ ] **Step 4: Verify**

Run:

```bash
cmake --build build --target MediaSdkPlaybackRuntimeFrameQueueTest --parallel
ctest --test-dir build -R media_sdk_playback_runtime_frame_queue --output-on-failure
python3 tests/media_sdk_playback_runtime_architecture_checks.py
```

Expected: all pass.

- [ ] **Step 5: Commit Task 3**

```bash
git add sdk/media_playback_runtime
git commit -m "[功能新增] 增加SDK运行时帧队列"
```

Stop after the commit and ask the user to confirm Task 4.

## Task 4: Master Clock And A/V Scheduler

**Files:**
- Create: `sdk/media_playback_runtime/src/MasterClock.h`
- Create: `sdk/media_playback_runtime/src/MasterClock.cpp`
- Create: `sdk/media_playback_runtime/src/AvSyncScheduler.h`
- Create: `sdk/media_playback_runtime/src/AvSyncScheduler.cpp`
- Create: `sdk/media_playback_runtime/tests/tst_av_sync_scheduler.cpp`
- Modify: `sdk/media_playback_runtime/CMakeLists.txt`

- [ ] **Step 1: Add scheduler tests**

Create tests covering these exact scenarios:

```cpp
int main()
{
    rendersFrameInsideThreshold();
    waitsForEarlyFrameWithSubmitLeadTime();
    dropsLateFrameBeyondThreshold();
    forcesRenderAfterEightConsecutiveDrops();
    usesVideoMonotonicClockWhenAudioClockInvalid();
    ignoresAudioClockFromOldGeneration();
}
```

Use these constants in the expected assertions:

```cpp
constexpr auto submitLeadTime = std::chrono::milliseconds(2);
constexpr auto lateDropThreshold = std::chrono::milliseconds(100);
constexpr auto maxScheduledWait = std::chrono::milliseconds(40);
constexpr int maxConsecutiveDropsBeforeForceRender = 8;
```

- [ ] **Step 2: Confirm failing test**

Register `MediaSdkPlaybackRuntimeAvSyncSchedulerTest`.

Run: `cmake --build build --target MediaSdkPlaybackRuntimeAvSyncSchedulerTest --parallel`

Expected: FAIL because scheduler headers are missing.

- [ ] **Step 3: Implement clock and scheduler**

Expose:

```cpp
enum class VideoScheduleAction {
    Render,
    Wait,
    Drop
};

struct VideoScheduleDecision {
    VideoScheduleAction action = VideoScheduleAction::Wait;
    std::chrono::microseconds waitTime { 0 };
    std::chrono::microseconds lateness { 0 };
    bool forcedRender = false;
};

struct AvSyncConfig {
    std::chrono::microseconds submitLeadTime { std::chrono::milliseconds(2) };
    std::chrono::microseconds lateDropThreshold { std::chrono::milliseconds(100) };
    std::chrono::microseconds maxScheduledWait { std::chrono::milliseconds(40) };
    int maxConsecutiveDropsBeforeForceRender = 8;
};

class AvSyncScheduler {
public:
    explicit AvSyncScheduler(AvSyncConfig config = {});
    void reset(Generation generation);
    VideoScheduleDecision decide(
        std::chrono::microseconds framePts,
        ClockSnapshot clock,
        Generation currentGeneration);
};
```

Rules:

- If `clock.valid` and `clock.generation == currentGeneration`, use `clock.position`.
- If audio clock is invalid, `MasterClock` uses monotonic video clock anchored by the first video frame.
- If `framePts - clock > submitLeadTime`, return `Wait` capped by `maxScheduledWait`.
- If `clock - framePts > lateDropThreshold`, return `Drop` until the forced render threshold is reached.
- Forced render resets consecutive drop count.

- [ ] **Step 4: Verify**

Run:

```bash
cmake --build build --target MediaSdkPlaybackRuntimeAvSyncSchedulerTest --parallel
ctest --test-dir build -R media_sdk_playback_runtime_av_sync_scheduler --output-on-failure
```

Expected: all pass.

- [ ] **Step 5: Commit Task 4**

```bash
git add sdk/media_playback_runtime
git commit -m "[功能新增] 增加SDK运行时音视频同步调度"
```

Stop after the commit and ask the user to confirm Task 5.

## Task 5: Present Tracker, Async Completion, And Pending Depth

**Files:**
- Create: `sdk/media_playback_runtime/src/PresentTracker.h`
- Create: `sdk/media_playback_runtime/src/PresentTracker.cpp`
- Create: `sdk/media_playback_runtime/tests/tst_present_tracker.cpp`
- Modify: `sdk/media_playback_runtime/CMakeLists.txt`

- [ ] **Step 1: Add tracker tests**

Create tests covering:

```cpp
int main()
{
    acceptsCompletionForCurrentSessionGenerationAndPresentId();
    ignoresCompletionFromOldGeneration();
    ignoresUnknownPresentId();
    reportsFailureForCurrentNativePresent();
    capsPendingDepthAndReplacesOlderSameGenerationFrame();
    clearCancelsAllPendingPresents();
}
```

- [ ] **Step 2: Confirm failing test**

Run: `cmake --build build --target MediaSdkPlaybackRuntimePresentTrackerTest --parallel`

Expected: FAIL because `PresentTracker` does not exist.

- [ ] **Step 3: Implement tracker**

Expose:

```cpp
enum class PresentCompletionAction {
    AcceptedSuccess,
    AcceptedFailure,
    IgnoredStale,
    IgnoredUnknown
};

struct TrackedPresent {
    PresentId id = 0;
    SessionId sessionId = 0;
    Generation generation = 0;
    bool nativeFrame = false;
};

class PresentTracker {
public:
    void reset(SessionId sessionId, Generation generation);
    void setMaxPending(std::size_t maxPending);
    bool track(TrackedPresent present);
    PresentCompletionAction complete(SessionId sessionId, Generation generation, PresentCompletion completion);
    void clear();
    std::size_t pendingCount() const;
};
```

Rules:

- Track present ids by `(sessionId, generation, presentId)`.
- Old generation completion must not trigger fallback.
- Current generation `UnsupportedNativeHandle`, `DeviceLost`, or `Failed` becomes `AcceptedFailure`.
- `clear()` removes pending ids and is called by stop, seek, fallback, and surface destruction.

- [ ] **Step 4: Verify**

Run:

```bash
cmake --build build --target MediaSdkPlaybackRuntimePresentTrackerTest --parallel
ctest --test-dir build -R media_sdk_playback_runtime_present_tracker --output-on-failure
python3 tests/media_sdk_playback_runtime_architecture_checks.py
```

Expected: all pass.

- [ ] **Step 5: Commit Task 5**

```bash
git add sdk/media_playback_runtime
git commit -m "[功能新增] 增加视频呈现完成跟踪"
```

Stop after the commit and ask the user to confirm Task 6.

## Task 6: Native Fallback State Machine

**Files:**
- Create: `sdk/media_playback_runtime/src/NativeFallbackController.h`
- Create: `sdk/media_playback_runtime/src/NativeFallbackController.cpp`
- Create: `sdk/media_playback_runtime/tests/tst_native_fallback_controller.cpp`
- Modify: `sdk/media_playback_runtime/CMakeLists.txt`

- [ ] **Step 1: Add fallback tests**

Create tests covering:

```cpp
int main()
{
    currentGenerationNativeFailureStartsFallbackPending();
    staleGenerationFailureIsCountedButDoesNotFallback();
    fallbackSwitchesCurrentSessionToCpuOnly();
    fallbackIncrementsGenerationAndRequiresSeekCompletionBeforeResume();
    fallbackRequiresAudioPauseFlushQueueAbortAndPresenterClear();
}
```

- [ ] **Step 2: Confirm failing test**

Run: `cmake --build build --target MediaSdkPlaybackRuntimeNativeFallbackControllerTest --parallel`

Expected: FAIL because controller does not exist.

- [ ] **Step 3: Implement fallback controller**

Expose:

```cpp
enum class RuntimePlaybackState {
    Idle,
    Opening,
    Playing,
    Paused,
    Seeking,
    FallbackPending,
    Draining,
    Stopped,
    Error
};

struct FallbackRequest {
    SessionId sessionId = 0;
    Generation generation = 0;
    PresentStatus reason = PresentStatus::Failed;
    std::chrono::microseconds resumePosition { 0 };
};

struct FallbackTransition {
    bool accepted = false;
    Generation newGeneration = 0;
    VideoOutputPolicy newPolicy = VideoOutputPolicy::PreferNative;
    bool pauseAudio = false;
    bool flushAudio = false;
    bool abortQueues = false;
    bool clearPresenter = false;
    bool requestCpuDecode = false;
};

class NativeFallbackController {
public:
    void reset(SessionId sessionId, Generation generation, VideoOutputPolicy policy);
    FallbackTransition beginFallback(FallbackRequest request);
    bool completeSeek(SessionId sessionId, Generation generation);
    RuntimePlaybackState state() const;
    VideoOutputPolicy policy() const;
};
```

Rules:

- Only current `(sessionId, generation)` failures can start fallback.
- `beginFallback()` must return all side effects needed by runtime orchestration: pause audio, flush audio, abort queues, clear presenter, request CPU decode.
- New policy is `CpuOnly` for the current session.
- New generation invalidates old EOF, audio frames, video frames, and present completions.
- Playback resumes only after seek completion for the fallback generation.

- [ ] **Step 4: Verify**

Run:

```bash
cmake --build build --target MediaSdkPlaybackRuntimeNativeFallbackControllerTest --parallel
ctest --test-dir build -R media_sdk_playback_runtime_native_fallback_controller --output-on-failure
```

Expected: all pass.

- [ ] **Step 5: Commit Task 6**

```bash
git add sdk/media_playback_runtime
git commit -m "[功能新增] 增加SDK运行时fallback状态机"
```

Stop after the commit and ask the user to confirm Task 7.

## Task 7: RuntimePlayer With Mock Audio And Mock Presenter

**Files:**
- Create: `sdk/media_playback_runtime/include/media_sdk/runtime/RuntimePlayer.h`
- Create: `sdk/media_playback_runtime/src/RuntimePlayer.cpp`
- Create: `sdk/media_playback_runtime/tests/tst_runtime_player_mock.cpp`
- Modify: `sdk/media_playback_runtime/CMakeLists.txt`

- [ ] **Step 1: Add runtime player tests with mocks**

Create mock integration tests covering:

```cpp
int main()
{
    openStartsNewSessionAndResetsQueues();
    audioFramesAreWrittenToInjectedAudioOutput();
    videoFramesAreScheduledAgainstAudioClock();
    eofCompletesOnlyAfterAudioAndVideoDrain();
    seekInvalidatesOldGenerationFramesAndCompletions();
    stopAbortsQueuesPausesAudioClearsPresenterAndReturnsIdle();
    nativePresenterFailureRunsFullFallbackTransition();
}
```

- [ ] **Step 2: Confirm failing test**

Run: `cmake --build build --target MediaSdkPlaybackRuntimePlayerMockTest --parallel`

Expected: FAIL because `RuntimePlayer` does not exist.

- [ ] **Step 3: Implement `RuntimePlayer` facade**

Expose a narrow first-version API:

```cpp
struct RuntimePlayerConfig {
    std::size_t audioQueueCapacity = 32;
    std::size_t videoQueueCapacity = 8;
    VideoOutputPolicy outputPolicy = VideoOutputPolicy::PreferNative;
    AvSyncConfig syncConfig {};
};

struct RuntimePlayerDependencies {
    IAudioOutput* audioOutput = nullptr;
    IVideoPresenter* videoPresenter = nullptr;
};

class RuntimePlayer final : private IVideoPresenterEvents {
public:
    RuntimePlayer(RuntimePlayerConfig config, RuntimePlayerDependencies dependencies);
    ~RuntimePlayer() override;

    Result<void> open();
    void enqueueAudio(RuntimeAudioFrame frame);
    void enqueueVideo(RuntimeVideoFrame frame);
    void enqueueEndOfStream(SessionId sessionId, Generation generation);
    void pause();
    void resume();
    void seek(std::chrono::microseconds position);
    void stop();
    RuntimeDiagnostics diagnostics() const;

private:
    void onPresentComplete(PresentCompletion completion) override;
};
```

Ownership model:

- `RuntimePlayer` does not own injected `IAudioOutput` or `IVideoPresenter`; the composition root must keep them alive until `RuntimePlayer::stop()` and destruction finish.
- Runtime queues own frame values while queued.
- Presenter may retain `VideoFrame` storage after `present()` until completion or `clear()`.
- Stop order is queue abort, audio pause/flush/close as appropriate, presenter clear, completion tracker clear.

- [ ] **Step 4: Verify**

Run:

```bash
cmake --build build --target MediaSdkPlaybackRuntimePlayerMockTest --parallel
ctest --test-dir build -R media_sdk_playback_runtime_player_mock --output-on-failure
python3 tests/media_sdk_playback_runtime_architecture_checks.py
```

Expected: all pass.

- [ ] **Step 5: Commit Task 7**

```bash
git add sdk/media_playback_runtime
git commit -m "[功能新增] 增加SDK播放运行时播放器"
```

Stop after the commit and ask the user to confirm Task 8.

## Task 8: macOS CoreAudio Output Target Skeleton

**Files:**
- Create: `sdk/media_platform_audio_macos/CMakeLists.txt`
- Create: `sdk/media_platform_audio_macos/include/media_sdk/platform/macos/CoreAudioAudioOutput.h`
- Create: `sdk/media_platform_audio_macos/src/CoreAudioAudioOutput.cpp`
- Create: `sdk/media_platform_audio_macos/tests/tst_core_audio_output_contract.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/media_sdk_playback_runtime_architecture_checks.py`

- [ ] **Step 1: Add platform architecture checks**

Extend Python checks so:

- `sdk/media_platform_audio_macos` exists only as a separate target.
- Runtime source does not include `media_sdk/platform/macos/CoreAudioAudioOutput.h`.
- Platform target links `media_sdk::playback_runtime`.
- Platform target links Apple frameworks only inside the platform target.

- [ ] **Step 2: Confirm failing check**

Run: `python3 tests/media_sdk_playback_runtime_architecture_checks.py`

Expected: FAIL because platform audio target does not exist.

- [ ] **Step 3: Add CoreAudio skeleton**

Create a concrete class with this shape:

```cpp
class CoreAudioAudioOutput final : public media_sdk::runtime::IAudioOutput {
public:
    CoreAudioAudioOutput();
    ~CoreAudioAudioOutput() override;

    media_sdk::Result<void> open(const media_sdk::runtime::AudioFormat& format) override;
    media_sdk::Result<void> write(media_sdk::runtime::AudioBufferView buffer) override;
    media_sdk::runtime::ClockSnapshot clock() const override;
    void pause() override;
    void resume() override;
    void flush() override;
    void close() override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
```

Initial implementation may return a structured unsupported-platform error when not built on Apple. On Apple, it must compile and expose deterministic open/write/pause/resume/flush/close state even before device callback is filled in.

- [ ] **Step 4: Register target**

Modify root `CMakeLists.txt`:

```cmake
if(APPLE)
    add_subdirectory(sdk/media_platform_audio_macos)
endif()
```

Platform target links:

```cmake
target_link_libraries(media_sdk_platform_audio_macos PUBLIC
    media_sdk::playback_runtime
)

target_link_libraries(media_sdk_platform_audio_macos PRIVATE
    "-framework CoreAudio"
    "-framework AudioToolbox"
)
```

- [ ] **Step 5: Verify**

Run:

```bash
python3 tests/media_sdk_playback_runtime_architecture_checks.py
cmake --build build --target media_sdk_platform_audio_macos --parallel
```

Expected: all pass on macOS.

- [ ] **Step 6: Commit Task 8**

```bash
git add CMakeLists.txt sdk/media_platform_audio_macos tests/media_sdk_playback_runtime_architecture_checks.py
git commit -m "[功能新增] 增加macOS CoreAudio输出骨架"
```

Stop after the commit and ask the user to confirm Task 9.

## Task 9: CoreAudio Device Clock And Audio Callback

**Files:**
- Modify: `sdk/media_platform_audio_macos/src/CoreAudioAudioOutput.cpp`
- Modify: `sdk/media_platform_audio_macos/include/media_sdk/platform/macos/CoreAudioAudioOutput.h`
- Create: `sdk/media_platform_audio_macos/src/CoreAudioRingBuffer.h`
- Create: `sdk/media_platform_audio_macos/tests/tst_core_audio_ring_buffer.cpp`
- Modify: `sdk/media_platform_audio_macos/CMakeLists.txt`

- [ ] **Step 1: Add ring buffer tests**

Create tests covering:

```cpp
int main()
{
    writeAndReadPreserveByteOrder();
    readSilenceWhenUnderrun();
    flushClearsQueuedBytesAndIncrementsGeneration();
    queuedDurationUsesFormatByteRate();
    closeWakesBlockedWriters();
}
```

- [ ] **Step 2: Implement CoreAudio ring buffer**

Use `std::mutex` and `std::condition_variable`; do not call Qt. The ring buffer stores PCM bytes plus generation and PTS tracking needed for `ClockSnapshot`.

- [ ] **Step 3: Implement CoreAudio callback lifecycle**

Rules:

- `open()` configures stream format from `AudioFormat`.
- `write()` pushes into the ring buffer and returns backpressure errors only when stopped or closed.
- Audio callback pulls data and writes silence on underrun.
- `clock().position` estimates hardware playback position, not submitted byte count.
- `flush()` clears ring buffer, resets anchor, increments clock generation.
- `pause()` stops device consumption without losing generation.
- `close()` stops device, wakes waiters, and releases CoreAudio resources through RAII.

- [ ] **Step 4: Verify**

Run:

```bash
cmake --build build --target MediaSdkPlatformAudioMacosRingBufferTest --parallel
ctest --test-dir build -R media_sdk_platform_audio_macos_ring_buffer --output-on-failure
cmake --build build --target media_sdk_platform_audio_macos --parallel
```

Expected: all pass.

- [ ] **Step 5: Commit Task 9**

```bash
git add sdk/media_platform_audio_macos
git commit -m "[功能新增] 实现CoreAudio音频输出"
```

Stop after the commit and ask the user to confirm Task 10.

## Task 10: Qt RHI Video Presenter Skeleton In PlayPlugin

**Files:**
- Create: `plugins/PlayPlugin/src/playback/QtRhiVideoPresenter.h`
- Create: `plugins/PlayPlugin/src/playback/QtRhiVideoPresenter.cpp`
- Create: `tests/playplugin_qt_rhi_presenter_architecture_checks.py`
- Modify: `plugins/PlayPlugin/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add PlayPlugin presenter architecture check**

Create a Python check verifying:

- `QtRhiVideoPresenter` includes `media_sdk/runtime/VideoPresenter.h`.
- `QtRhiVideoPresenter` is the only new file allowed to mention `QRhi` or Scene Graph presenter internals.
- `sdk/media_playback_runtime` does not mention PlayPlugin, `FFmpegSurface`, or Qt types.
- Presenter implements `setEvents()` and uses `PresentCompletion` for async failure.

- [ ] **Step 2: Confirm failing check**

Run: `python3 tests/playplugin_qt_rhi_presenter_architecture_checks.py`

Expected: FAIL because presenter files do not exist.

- [ ] **Step 3: Add presenter skeleton**

Public class shape:

```cpp
class QtRhiVideoPresenter final : public media_sdk::runtime::IVideoPresenter {
public:
    explicit QtRhiVideoPresenter(FFmpegSurface* surface);
    ~QtRhiVideoPresenter() override;

    media_sdk::runtime::VideoPresenterCapabilities capabilities() const override;
    void setEvents(media_sdk::runtime::IVideoPresenterEvents* events) override;
    media_sdk::runtime::PresentResult present(
        media_sdk::VideoFrame frame,
        media_sdk::runtime::PresentTiming timing) override;
    void clear() override;

private:
    QPointer<FFmpegSurface> m_surface;
    media_sdk::runtime::IVideoPresenterEvents* m_events = nullptr;
    std::atomic_uint64_t m_nextPresentId { 0 };
};
```

Threading and lifetime rules:

- `FFmpegSurface*` is non-owning and guarded by `QPointer`.
- `present()` never touches Scene Graph state directly from the runtime thread.
- `present()` stores or moves `VideoFrame` into the Qt-side pending operation so native storage stays alive.
- `clear()` cancels pending operations and posts a surface clear on the correct Qt thread.
- Async completion must include the present id returned by `present()`.

- [ ] **Step 4: Register check and build**

Add CTest registration for `playplugin_qt_rhi_presenter_architecture_checks`.

Run:

```bash
python3 tests/playplugin_qt_rhi_presenter_architecture_checks.py
cmake --build build --target PlayPlugin --parallel
```

Expected: all pass.

- [ ] **Step 5: Commit Task 10**

```bash
git add CMakeLists.txt plugins/PlayPlugin tests/playplugin_qt_rhi_presenter_architecture_checks.py
git commit -m "[功能新增] 增加Qt RHI视频呈现器"
```

Stop after the commit and ask the user to confirm Task 11.

## Task 11: VideoToolbox Native Path To Qt Presenter

**Files:**
- Modify: `plugins/PlayPlugin/src/playback/QtRhiVideoPresenter.cpp`
- Modify: existing PlayPlugin Metal bridge files found by `rg "AppleMetalVideoTextureBridge|CVMetalTexture|createFrom" plugins/PlayPlugin`
- Modify: existing surface/material files found by `rg "FFmpegSurface|VideoMaterial|VideoNode" plugins/PlayPlugin/src`
- Modify: `tests/playplugin_qt_rhi_presenter_architecture_checks.py`

- [ ] **Step 1: Add native-path assertions**

Extend checks to reject:

- Dropping `PixelFormat::Native` in new runtime presenter path.
- Calling `sws_scale` in the new presenter path.
- Copying native frames into CPU planes before Qt RHI presentation.

The check must require diagnostic tokens:

```python
for token in ("nativeTextureCreated", "nativeTextureDrawn", "cpuMemcpy", "cpuTransferred"):
    assert_contains(source_text, token, source_path)
```

- [ ] **Step 2: Confirm failing check**

Run: `python3 tests/playplugin_qt_rhi_presenter_architecture_checks.py`

Expected: FAIL until native diagnostic path is implemented.

- [ ] **Step 3: Wire native handle lifecycle**

Implementation rules:

- Accept `media_sdk::VideoFrame` with `PixelFormat::Native` and `NativeHandleKind::VideoToolboxPixelBuffer`.
- Keep `VideoFrame` value alive while Qt side creates and draws the native texture.
- Create `CVMetalTextureRef` through the existing Apple Metal bridge.
- Hold `CVMetalTextureRef` through an RAII wrapper that outlives the `QRhiTexture` native wrapper use.
- Release order is fixed: detach/replace `QRhiTexture`, then release `CVMetalTextureRef`, then release retained frame storage.
- Report `UnsupportedNativeHandle` through `PresentCompletion` if the presenter cannot use the native handle.

- [ ] **Step 4: Verify**

Run:

```bash
python3 tests/playplugin_qt_rhi_presenter_architecture_checks.py
cmake --build build --target PlayPlugin --parallel
```

Expected: all pass. Manual video verification is reserved for Task 15 after runtime switch exists.

- [ ] **Step 5: Commit Task 11**

```bash
git add plugins/PlayPlugin tests/playplugin_qt_rhi_presenter_architecture_checks.py
git commit -m "[功能修改] 打通VideoToolbox零拷贝呈现路径"
```

Stop after the commit and ask the user to confirm Task 12.

## Task 12: Runtime Mode Switch And Old/New Chain Mutual Exclusion

**Files:**
- Modify: PlayPlugin playback engine files found by `rg "class PlayerEngine|PlaybackDataBridge|QtPlaybackAdapter|AudioRenderer|VideoRenderer" plugins/PlayPlugin/src`
- Modify: `plugins/PlayPlugin/CMakeLists.txt`
- Modify: `tests/playplugin_qt_rhi_presenter_architecture_checks.py`

- [ ] **Step 1: Add mode-switch architecture assertions**

The check must require:

- An enum equivalent to `PlaybackRuntimeMode { LegacyQt, SdkRuntime }`.
- A stop/close transition before switching modes during playback.
- Only one chain owns audio output at a time.
- Only one chain binds the same `FFmpegSurface` at a time.
- Switching mode clears surface, completion tracker, queues, and error state.

- [ ] **Step 2: Confirm failing check**

Run: `python3 tests/playplugin_qt_rhi_presenter_architecture_checks.py`

Expected: FAIL until mode switch code exists.

- [ ] **Step 3: Implement mode switch**

Required behavior:

- Default mode remains legacy.
- `SdkRuntime` mode creates `RuntimePlayer`, `CoreAudioAudioOutput`, and `QtRhiVideoPresenter` in the PlayPlugin composition root.
- Changing mode while playing calls the same stop path used by user stop, waits for old chain shutdown, clears presenter/surface, then constructs the selected chain.
- Legacy chain and SDK runtime chain cannot both own the audio device.
- Legacy chain and SDK runtime chain cannot both push frames into the same `FFmpegSurface`.

- [ ] **Step 4: Verify**

Run:

```bash
python3 tests/playplugin_qt_rhi_presenter_architecture_checks.py
cmake --build build --target PlayPlugin --parallel
```

Expected: all pass.

- [ ] **Step 5: Commit Task 12**

```bash
git add plugins/PlayPlugin tests/playplugin_qt_rhi_presenter_architecture_checks.py
git commit -m "[功能新增] 增加SDK播放运行时切换开关"
```

Stop after the commit and ask the user to confirm Task 13.

## Task 13: Native Failure Fallback End-To-End

**Files:**
- Modify: `sdk/media_playback_runtime/src/RuntimePlayer.cpp`
- Modify: `plugins/PlayPlugin/src/playback/QtRhiVideoPresenter.cpp`
- Add or modify runtime mock tests under `sdk/media_playback_runtime/tests/`

- [ ] **Step 1: Add fallback integration test**

Create or extend a runtime mock test covering:

```cpp
int main()
{
    currentGenerationDeviceLostPausesAudioFlushesQueuesClearsPresenterAndRequestsCpuOnlyDecode();
    oldGenerationDeviceLostDoesNotInterruptCurrentPlayback();
    fallbackSeekCompletionResumesAudioAndVideoScheduling();
}
```

- [ ] **Step 2: Confirm failing test**

Run: `ctest --test-dir build -R media_sdk_playback_runtime_player_mock --output-on-failure`

Expected: FAIL until runtime player applies fallback transitions end-to-end.

- [ ] **Step 3: Implement fallback orchestration**

Runtime must perform the full transition from the design document:

1. Validate present completion session, generation, and present id.
2. Enter `FallbackPending`.
3. Pause video scheduling.
4. Pause audio output.
5. Switch current session policy to `CpuOnly`.
6. Abort audio and video queues.
7. Flush audio output and increment clock generation.
8. Clear pending native frames and presenter state.
9. Increment fallback generation.
10. Request core decode with `preferNativeVideoFrames=false`.
11. Seek near the current audio clock position.
12. Resume only after new-generation seek completion.

- [ ] **Step 4: Verify**

Run:

```bash
cmake --build build --target MediaSdkPlaybackRuntimePlayerMockTest --parallel
ctest --test-dir build -R media_sdk_playback_runtime_player_mock --output-on-failure
cmake --build build --target PlayPlugin --parallel
```

Expected: all pass.

- [ ] **Step 5: Commit Task 13**

```bash
git add sdk/media_playback_runtime plugins/PlayPlugin
git commit -m "[功能修复] 增加native失败fallback闭环"
```

Stop after the commit and ask the user to confirm Task 14.

## Task 14: Runtime Diagnostics And Zero-Copy Regression Gates

**Files:**
- Modify: `sdk/media_playback_runtime/include/media_sdk/runtime/RuntimeTypes.h`
- Modify: `sdk/media_playback_runtime/src/RuntimePlayer.cpp`
- Modify: `plugins/PlayPlugin/src/playback/QtRhiVideoPresenter.cpp`
- Modify: `tests/media_sdk_playback_runtime_architecture_checks.py`
- Modify: `tests/playplugin_qt_rhi_presenter_architecture_checks.py`

- [ ] **Step 1: Add diagnostics tests**

Tests must assert:

- Native frame accepted increments `nativeAccepted`.
- Native present queued increments `nativePresented`.
- Native texture creation increments `nativeTextureCreated`.
- Native texture draw increments `nativeTextureDrawn`.
- CPU fallback increments `nativeFallbacks` and then `cpuPresented`.
- Native path success keeps `cpuCopied == 0`, `cpuTransferred == 0`, and `cpuMemcpy == 0`.
- Queue abort increments `queueAbortCount`.
- EOF after drain increments `eofPresented`.

- [ ] **Step 2: Confirm failing diagnostics test**

Run: `ctest --test-dir build -R "media_sdk_playback_runtime|playplugin_qt_rhi" --output-on-failure`

Expected: FAIL until diagnostics are wired.

- [ ] **Step 3: Implement diagnostics**

Rules:

- Runtime owns session-level counters and exposes a snapshot by value.
- Presenter reports texture counters to runtime through completion detail or a small event callback without adding Qt types to runtime.
- Diagnostics names must match the design document exactly.
- The zero-copy success condition is loggable as:

```text
nativePresented > 0
nativeTextureCreated > 0
nativeTextureDrawn > 0
cpuCopied == 0
cpuTransferred == 0
cpuMemcpy == 0
nativeFallbacks == 0
```

- [ ] **Step 4: Verify**

Run:

```bash
python3 tests/media_sdk_playback_runtime_architecture_checks.py
python3 tests/playplugin_qt_rhi_presenter_architecture_checks.py
ctest --test-dir build -R "media_sdk_playback_runtime|playplugin_qt_rhi" --output-on-failure
cmake --build build --parallel
```

Expected: all pass.

- [ ] **Step 5: Commit Task 14**

```bash
git add sdk/media_playback_runtime plugins/PlayPlugin tests
git commit -m "[测试] 增加SDK运行时诊断和回归门禁"
```

Stop after the commit and ask the user to confirm Task 15.

## Task 15: macOS Manual Verification And Documentation Sync

**Files:**
- Modify: `docs/superpowers/specs/2026-06-24-sdk-playback-runtime-macos-design.md`
- Modify: this plan only if completed task status needs to be checked off during execution.

- [ ] **Step 1: Run full automated verification**

Run:

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: all configured build targets and tests pass.

- [ ] **Step 2: Run macOS manual playback verification**

Run the app:

```bash
./build/app/PluginBasedApp
```

Manual checks:

- Legacy mode still plays the same sample set as before.
- SDK runtime mode has video picture.
- SDK runtime mode has normal audio.
- Pause/resume does not freeze.
- Seek does not show old-generation frames.
- EOF triggers only after audio/video drain.
- Native path logs show `nativePresented > 0`, `nativeTextureCreated > 0`, `nativeTextureDrawn > 0`.
- Native path logs show `cpuCopied == 0`, `cpuTransferred == 0`, `cpuMemcpy == 0`.
- Simulated native failure falls back to CPU without playback interruption.

- [ ] **Step 3: Sync implementation notes into design doc**

Update the design doc only with facts observed during implementation:

- Final target names.
- Final ownership rules.
- Final diagnostics names.
- Known macOS verification results.
- Remaining platform gaps for Windows/Linux/OpenGL.

- [ ] **Step 4: Commit Task 15**

```bash
git add docs/superpowers/specs/2026-06-24-sdk-playback-runtime-macos-design.md docs/superpowers/plans/2026-06-24-sdk-playback-runtime-macos.md
git commit -m "[文档] 同步SDK播放运行时实现结果"
```

Stop after the commit and report final verification results.

## Self-Review

### Spec Coverage

- SDK runtime target, interfaces, queues, backpressure, A/V sync, EOF drain, and fallback are covered by Tasks 1-7 and 13-14.
- CoreAudio audio output and clock are covered by Tasks 8-9.
- Qt RHI presenter and VideoToolbox native path are covered by Tasks 10-11.
- Old/new playback chain coexistence and mutual exclusion are covered by Task 12.
- Diagnostics and zero-copy proof are covered by Task 14.
- Full macOS verification and documentation synchronization are covered by Task 15.

### Placeholder Scan

This plan intentionally avoids undefined placeholders. Every task has file paths, required contracts, concrete command lines, expected results, and commit messages.

### Type Consistency

- `SessionId`, `Generation`, `PresentId`, `PresentStatus`, `PresentCompletion`, `VideoOutputPolicy`, `ClockSnapshot`, and `RuntimeDiagnostics` are introduced before later tasks use them.
- Runtime depends on `IAudioOutput` and `IVideoPresenter`; `CoreAudioAudioOutput` and `QtRhiVideoPresenter` remain concrete implementations outside the runtime target.
- Fallback semantics use the same session/generation filtering in `PresentTracker`, `NativeFallbackController`, and `RuntimePlayer`.
