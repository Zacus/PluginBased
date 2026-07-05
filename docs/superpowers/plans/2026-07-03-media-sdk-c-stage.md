# Media SDK C Stage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move playback session orchestration from PlayPlugin into SDK while keeping Qt rendering as an injected presenter.

**Architecture:** Add a new Qt-free `media_sdk_playback_session` layer that composes `media_sdk::Player`, `runtime::RuntimePlayer`, injected `IAudioOutput`, and injected `IVideoPresenter`. Keep the current single decode worker model and migrate session/generation/seek/fallback/EOF orchestration out of `SdkPlaybackAdapter` in controlled phases.

**Tech Stack:** C++20 SDK modules, Qt 6 PlayPlugin adapter, FFmpeg frame contracts, CoreAudio AudioUnit output, CMake/CTest, Python architecture checks.

---

## Execution Rules

- Execute one task at a time.
- Each task ends with one focused commit.
- Stop after each commit and ask the user to confirm before starting the next task.
- Do not split demux/audio decode/video decode into separate threads in this C stage.
- Do not put Qt, QObject, QQuickWindow, QSGNode, QRhi, or QML types into SDK public headers.
- Do not hide backpressure with unbounded queues, timer retry, or arbitrary capacity increases.
- Keep PlayPlugin as the owner of concrete Qt presenter and, for now, concrete macOS audio output.
- Keep `media_sdk_core` independent from `media_sdk_playback_runtime`.
- New C stage composition belongs in `sdk/media_playback_session`.

## Current Baseline

The current main path is:

```text
PlayPlugin::PlayerEngine
  -> PlayPlugin::PlaybackPipeline
  -> PlayPlugin::SdkPlaybackAdapter
      -> media_sdk::Player
      -> media_sdk::runtime::RuntimePlayer
      -> CoreAudioAudioOutput
      -> QtRhiVideoPresenter
```

`SdkPlaybackAdapter` currently owns too much:

- `media_sdk::Player`
- `runtime::RuntimePlayer`
- `IEventSink`
- `IDecodeFrameSink`
- `IRuntimePlayerEvents`
- core timeline to runtime timeline mapping
- seek completion mapping
- native fallback re-open/re-seek
- EOF handoff from core to runtime
- audio sample format conversion

C stage moves those SDK responsibilities into `media_sdk_playback_session`.

## File Responsibility Map

### Create

- `sdk/media_playback_session/CMakeLists.txt`
  - Defines `media_sdk_playback_session` static library and tests.
- `sdk/media_playback_session/include/media_sdk/session/PlaybackSession.h`
  - Public SDK session facade.
- `sdk/media_playback_session/include/media_sdk/session/PlaybackSessionTypes.h`
  - Session config, dependencies, events, diagnostics policy types.
- `sdk/media_playback_session/src/PlaybackSession.cpp`
  - Owns core player and runtime player; implements public commands.
- `sdk/media_playback_session/src/SessionFrameRouter.h`
  - Internal `IDecodeFrameSink` implementation and push result mapping.
- `sdk/media_playback_session/src/SessionEventRouter.h`
  - Internal `IEventSink` implementation and control event routing.
- `sdk/media_playback_session/src/SessionTimeline.h`
  - Core metadata to runtime timeline mapping and stale generation checks.
- `sdk/media_playback_session/src/SessionAudioConverter.h`
  - SDK-side conversion to runtime-supported Float32 interleaved audio.
- `sdk/media_playback_session/tests/tst_playback_session_contract.cpp`
  - Public API and dependency contract tests.
- `sdk/media_playback_session/tests/tst_playback_session_timeline.cpp`
  - Timeline, seek, stale event, and generation tests.
- `sdk/media_playback_session/tests/tst_playback_session_fallback.cpp`
  - Native fallback tests using fake presenter/runtime dependencies.
- `sdk/media_playback_session/tests/tst_playback_session_audio_converter.cpp`
  - PCM conversion tests.
- `tests/media_sdk_playback_session_architecture_checks.py`
  - Qt-free and responsibility architecture checks.

### Modify

- `CMakeLists.txt`
  - Add `add_subdirectory(sdk/media_playback_session)` and CTest architecture check.
- `plugins/PlayPlugin/CMakeLists.txt`
  - Link `media_sdk::playback_session`.
- `plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.h/.cpp`
  - Phase down to Qt-facing adapter around `PlaybackSession`.
- `plugins/PlayPlugin/src/playback/PlaybackPipeline.h/.cpp`
  - Construct `PlaybackSession` dependencies.
- `sdk/media_core/include/media_sdk/PlayerConfig.h`
  - Keep config as SDK core input; avoid adding runtime concerns here unless needed.
- `sdk/media_core/include/media_sdk/MediaEvents.h`
  - Add session-facing event fields only if needed by tests; avoid event bloat.
- `sdk/media_playback_runtime/include/media_sdk/runtime/RuntimePlayer.h`
  - Add hooks only if session tests expose missing control APIs.

## Verification Commands

Use focused commands after each task:

```bash
cmake --build build --target MediaSdkPlaybackSessionContractTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_playback_session"
cmake --build build --target PlayPlugin --parallel
ctest --test-dir build --output-on-failure -R "playplugin|media_sdk_playback_session|media_sdk_playback_runtime|media_sdk_core"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

If `build/` is missing, configure using the repository command from `AGENTS.md`.

---

## Task 1: Add Playback Session Module Skeleton

**Purpose:** Add a Qt-free SDK session target without changing runtime behavior.

**Files:**
- Create: `sdk/media_playback_session/CMakeLists.txt`
- Create: `sdk/media_playback_session/include/media_sdk/session/PlaybackSessionTypes.h`
- Create: `sdk/media_playback_session/include/media_sdk/session/PlaybackSession.h`
- Create: `sdk/media_playback_session/src/PlaybackSession.cpp`
- Create: `sdk/media_playback_session/tests/tst_playback_session_contract.cpp`
- Modify: `CMakeLists.txt`
- Create: `tests/media_sdk_playback_session_architecture_checks.py`

- [ ] **Step 1: Add failing architecture check**

Create `tests/media_sdk_playback_session_architecture_checks.py`:

```python
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SESSION = ROOT / "sdk" / "media_playback_session"

def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")

def assert_contains(text: str, needle: str, path: Path) -> None:
    assert needle in text, f"{path} must contain {needle}"

def assert_not_contains(text: str, needle: str, path: Path) -> None:
    assert needle not in text, f"{path} must not contain {needle}"

def main() -> None:
    header = SESSION / "include" / "media_sdk" / "session" / "PlaybackSession.h"
    types = SESSION / "include" / "media_sdk" / "session" / "PlaybackSessionTypes.h"
    cmake = SESSION / "CMakeLists.txt"

    assert header.exists(), "PlaybackSession.h must exist"
    assert types.exists(), "PlaybackSessionTypes.h must exist"
    assert cmake.exists(), "media_playback_session CMakeLists.txt must exist"

    for path in [header, types]:
        text = read(path)
        assert_contains(text, "namespace media_sdk::session", path)
        assert_not_contains(text, "QObject", path)
        assert_not_contains(text, "QQuick", path)
        assert_not_contains(text, "QRhi", path)
        assert_not_contains(text, "QSG", path)

    assert_contains(read(cmake), "media_sdk_playback_session", cmake)

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Add CTest registration**

Modify root `CMakeLists.txt` under `if(BUILD_TESTING)`:

```cmake
add_test(NAME media_sdk_playback_session_architecture_checks
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/media_sdk_playback_session_architecture_checks.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
```

- [ ] **Step 3: Run and verify failure**

Run:

```bash
ctest --test-dir build --output-on-failure -R "media_sdk_playback_session_architecture_checks"
```

Expected: FAIL because the new module files do not exist.

- [ ] **Step 4: Add public session types**

Create `PlaybackSessionTypes.h` with:

```cpp
#pragma once

#include "media_sdk/MediaEvents.h"
#include "media_sdk/PlayerConfig.h"
#include "media_sdk/runtime/RuntimePlayer.h"

namespace media_sdk::session {

class ISessionEvents {
public:
    virtual ~ISessionEvents() = default;
    virtual void onEvent(const PlayerEvent& event) = 0;
    virtual void onRuntimeDiagnostics(runtime::RuntimeDiagnostics) {}
};

struct PlaybackSessionConfig {
    PlayerConfig core {};
    runtime::RuntimePlayerConfig runtime {};
    bool preferNativeVideoFrames = true;
};

struct PlaybackSessionDependencies {
    runtime::IAudioOutput* audioOutput = nullptr;
    runtime::IVideoPresenter* videoPresenter = nullptr;
    ISessionEvents* events = nullptr;
};

} // namespace media_sdk::session
```

- [ ] **Step 5: Add public session facade**

Create `PlaybackSession.h` with:

```cpp
#pragma once

#include "media_sdk/Result.h"
#include "media_sdk/runtime/RuntimeTypes.h"
#include "media_sdk/session/PlaybackSessionTypes.h"

#include <chrono>
#include <filesystem>
#include <memory>

namespace media_sdk::session {

class PlaybackSession final {
public:
    PlaybackSession(PlaybackSessionConfig config,
                    PlaybackSessionDependencies dependencies);
    ~PlaybackSession();

    PlaybackSession(const PlaybackSession&) = delete;
    PlaybackSession& operator=(const PlaybackSession&) = delete;

    [[nodiscard("open result determines whether playback session is usable")]]
    Result<void> open(const std::filesystem::path& path);
    void play();
    void pause();
    void stop();
    [[nodiscard("seek result determines whether the seek command was accepted")]]
    Result<void> seek(std::chrono::milliseconds position);
    [[nodiscard("timeline is required for stale callback checks")]]
    runtime::RuntimeTimeline timeline() const;
    [[nodiscard("diagnostics verify queue, fallback, native, and clock behavior")]]
    runtime::RuntimeDiagnostics diagnostics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace media_sdk::session
```

- [ ] **Step 6: Add minimal implementation**

Create `PlaybackSession.cpp`:

```cpp
#include "media_sdk/session/PlaybackSession.h"

#include "media_sdk/Error.h"

#include <utility>

namespace media_sdk::session {
namespace {

Result<void> notImplemented()
{
    return Result<void>::failure({
        .code = MediaErrorCode::InternalStateError,
        .message = "PlaybackSession is not wired yet",
        .detail = {},
    });
}

} // namespace

struct PlaybackSession::Impl {
    PlaybackSessionConfig config;
    PlaybackSessionDependencies dependencies;
};

PlaybackSession::PlaybackSession(PlaybackSessionConfig config,
                                 PlaybackSessionDependencies dependencies)
    : m_impl(std::make_unique<Impl>(Impl {
          .config = std::move(config),
          .dependencies = dependencies,
      }))
{
}

PlaybackSession::~PlaybackSession() = default;

Result<void> PlaybackSession::open(const std::filesystem::path&)
{
    return notImplemented();
}

void PlaybackSession::play() {}
void PlaybackSession::pause() {}
void PlaybackSession::stop() {}

Result<void> PlaybackSession::seek(std::chrono::milliseconds)
{
    return notImplemented();
}

runtime::RuntimeTimeline PlaybackSession::timeline() const
{
    return {};
}

runtime::RuntimeDiagnostics PlaybackSession::diagnostics() const
{
    return {};
}

} // namespace media_sdk::session
```

- [ ] **Step 7: Add target and contract test**

Create `sdk/media_playback_session/CMakeLists.txt` with a static library linked to core and runtime. Add `MediaSdkPlaybackSessionContractTest` that includes `PlaybackSession.h`, constructs a session with empty dependencies, and verifies `open({})` returns failure.

- [ ] **Step 8: Wire root CMake**

Modify root `CMakeLists.txt`:

```cmake
add_subdirectory(sdk/media_playback_session)
```

Place it after `sdk/media_playback_runtime`.

- [ ] **Step 9: Verify Task 1**

Run:

```bash
cmake --build build --target MediaSdkPlaybackSessionContractTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_playback_session"
```

Expected: PASS.

- [ ] **Step 10: Commit Task 1**

```bash
git add CMakeLists.txt sdk/media_playback_session tests/media_sdk_playback_session_architecture_checks.py
git commit -m "[架构新增] 增加SDK播放会话模块骨架"
```

Stop and ask for confirmation before Task 2.

---

## Task 2: Move Timeline Mapping Into SDK Session

**Purpose:** Move core metadata to runtime timeline mapping out of PlayPlugin.

**Files:**
- Create: `sdk/media_playback_session/src/SessionTimeline.h`
- Create: `sdk/media_playback_session/tests/tst_playback_session_timeline.cpp`
- Modify: `sdk/media_playback_session/CMakeLists.txt`

- [ ] **Step 1: Add timeline tests**

Test cases:

- Initial state rejects all core events.
- `acceptCoreTimeline(coreMetadata, runtimeTimeline)` opens frame acceptance.
- Matching core metadata returns runtime timeline.
- Stale session is rejected.
- Stale generation is rejected.
- `clear()` rejects everything.

- [ ] **Step 2: Implement `SessionTimeline`**

Create a small value/lock-free class if used only on session worker thread; otherwise use internal mutex at `PlaybackSession::Impl` level. The type should expose:

```cpp
class SessionTimeline {
public:
    void acceptCoreTimeline(EventMetadata core, runtime::RuntimeTimeline runtime);
    void clear();
    [[nodiscard]] bool acceptsCoreEvent(EventMetadata core) const;
    [[nodiscard]] std::optional<runtime::RuntimeTimeline> runtimeForCoreEvent(EventMetadata core) const;
    [[nodiscard]] bool acceptsRuntimeTimeline(runtime::RuntimeTimeline runtime) const;
};
```

- [ ] **Step 3: Verify Task 2**

Run:

```bash
cmake --build build --target MediaSdkPlaybackSessionTimelineTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_playback_session_timeline"
```

- [ ] **Step 4: Commit Task 2**

```bash
git add sdk/media_playback_session
git commit -m "[架构新增] 增加播放会话时间线映射"
```

Stop and ask for confirmation before Task 3.

---

## Task 3: Move Audio Conversion Into SDK Session

**Purpose:** Remove PCM conversion responsibility from PlayPlugin.

**Files:**
- Create: `sdk/media_playback_session/src/SessionAudioConverter.h`
- Create: `sdk/media_playback_session/tests/tst_playback_session_audio_converter.cpp`
- Modify later in Task 7: `plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp`

- [ ] **Step 1: Add converter tests**

Test cases:

- Float32 interleaved input is preserved byte-for-byte.
- Signed16 interleaved converts to normalized Float32.
- Signed32 interleaved converts to normalized Float32.
- Unknown format produces silence for one frame worth of channel count or returns structured failure. Choose failure unless existing playback depends on silence fallback.

- [ ] **Step 2: Implement converter**

Expose internal helper:

```cpp
struct ConvertedAudioFrame {
    AudioFrame frame;
    runtime::AudioSampleFormat runtimeFormat = runtime::AudioSampleFormat::Unknown;
};

[[nodiscard("audio conversion can fail for unsupported SDK sample formats")]]
Result<ConvertedAudioFrame> convertToRuntimeAudioFrame(const AudioFrame& frame);
```

Do not use Qt, FFmpeg, or PlayPlugin headers.

- [ ] **Step 3: Verify Task 3**

Run:

```bash
cmake --build build --target MediaSdkPlaybackSessionAudioConverterTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_playback_session_audio_converter"
```

- [ ] **Step 4: Commit Task 3**

```bash
git add sdk/media_playback_session
git commit -m "[架构新增] 将音频格式转换收进SDK会话"
```

Stop and ask for confirmation before Task 4.

---

## Task 4: Add Session Frame Router

**Purpose:** Make SDK session implement `IDecodeFrameSink` and enqueue decoded frames into runtime.

**Files:**
- Create: `sdk/media_playback_session/src/SessionFrameRouter.h`
- Create: `sdk/media_playback_session/tests/tst_playback_session_frame_router.cpp`
- Modify: `sdk/media_playback_session/CMakeLists.txt`

- [ ] **Step 1: Add fake runtime/player tests**

Test cases:

- Matching core timeline audio frame calls runtime `enqueueAudio()`.
- Matching core timeline video frame calls runtime `enqueueVideo()`.
- Stale core generation maps to `DecodeFramePushStatus::StaleGeneration`.
- Runtime `Backpressured` maps to decode `Backpressured`.
- Runtime `Cancelled` maps to decode `Cancelled`.
- Runtime `Closed` maps to decode `Closed`.

- [ ] **Step 2: Implement router**

Router responsibilities:

- Own no external object.
- Receive `AudioFrame` / `VideoFrame` and `DecodeFrameMetadata`.
- Ask `SessionTimeline` for runtime timeline.
- Convert audio through `SessionAudioConverter`.
- Build `runtime::RuntimeAudioFrame` / `RuntimeVideoFrame`.
- Map runtime push result back to core push result.

- [ ] **Step 3: Verify Task 4**

Run:

```bash
cmake --build build --target MediaSdkPlaybackSessionFrameRouterTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_playback_session_frame_router"
```

- [ ] **Step 4: Commit Task 4**

```bash
git add sdk/media_playback_session
git commit -m "[架构新增] 增加SDK会话帧路由"
```

Stop and ask for confirmation before Task 5.

---

## Task 5: Add Session Event Router And EOF Contract

**Purpose:** Route core control events inside SDK session and change external EOF to runtime-drained EOF.

**Files:**
- Create: `sdk/media_playback_session/src/SessionEventRouter.h`
- Create: `sdk/media_playback_session/tests/tst_playback_session_event_router.cpp`
- Modify: `sdk/media_playback_session/CMakeLists.txt`

- [ ] **Step 1: Add event router tests**

Test cases:

- `MediaInfoEvent` triggers runtime creation/open boundary before frame acceptance.
- `PositionChangedEvent` is forwarded only for accepted core timeline.
- `SeekCompletedEvent` completes timeline and resumes frame acceptance.
- Core `EndOfFileEvent` enqueues runtime EOF but does not emit external EOF immediately.
- Runtime `onEndOfStreamPresented()` emits external `EndOfFileEvent`.

- [ ] **Step 2: Implement router**

Router responsibilities:

- Keep external event sink fast.
- Keep core EOF and runtime EOF distinct.
- Forward media info/state/position/seek/error with stale filtering.
- Provide explicit callback for runtime EOF completion.

- [ ] **Step 3: Verify Task 5**

Run:

```bash
cmake --build build --target MediaSdkPlaybackSessionEventRouterTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_playback_session_event_router"
```

- [ ] **Step 4: Commit Task 5**

```bash
git add sdk/media_playback_session
git commit -m "[架构新增] 增加SDK会话事件路由"
```

Stop and ask for confirmation before Task 6.

---

## Task 6: Wire PlaybackSession To Core Player And RuntimePlayer

**Purpose:** Make `PlaybackSession` the SDK owner of `media_sdk::Player` and `runtime::RuntimePlayer`.

**Files:**
- Modify: `sdk/media_playback_session/src/PlaybackSession.cpp`
- Modify: `sdk/media_playback_session/include/media_sdk/session/PlaybackSession.h`
- Modify: `sdk/media_playback_session/tests/tst_playback_session_contract.cpp`

- [ ] **Step 1: Add integration-style fake dependency tests**

Test cases:

- Missing audio output returns open failure for media with audio.
- Missing video presenter returns open failure.
- `open(path)` creates core player and waits for media info boundary before runtime frame acceptance.
- `play()` calls core play and runtime resume.
- `pause()` calls core pause and runtime pause.
- `seek()` calls runtime seek before core seek.
- `stop()` stops core and runtime exactly once.

- [ ] **Step 2: Implement `PlaybackSession::Impl`**

Implementation constraints:

- `Impl` privately implements `IEventSink`, `IDecodeFrameSink`, and `runtime::IRuntimePlayerEvents`.
- `Impl` owns `std::unique_ptr<Player>`.
- `Impl` owns `std::shared_ptr<runtime::RuntimePlayer>` or `std::unique_ptr<runtime::RuntimePlayer>`. Prefer `unique_ptr` unless callbacks require shared lifetime.
- Public methods use a mutex only for state transfer; do not call presenter/audio/core while holding locks when avoidable.
- Destructor calls `stop()`.

- [ ] **Step 3: Verify Task 6**

Run:

```bash
cmake --build build --target MediaSdkPlaybackSessionContractTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_playback_session_contract"
```

- [ ] **Step 4: Commit Task 6**

```bash
git add sdk/media_playback_session
git commit -m "[架构新增] 接入SDK播放会话组合层"
```

Stop and ask for confirmation before Task 7.

---

## Task 7: Move Native Fallback Handling Into PlaybackSession

**Purpose:** Remove native fallback re-open/re-seek responsibility from PlayPlugin.

**Files:**
- Modify: `sdk/media_playback_session/src/PlaybackSession.cpp`
- Create/modify: `sdk/media_playback_session/tests/tst_playback_session_fallback.cpp`
- Modify later in Task 8: `plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp`

- [ ] **Step 1: Add fallback tests**

Test cases:

- Runtime native failure calls session fallback handler.
- Session updates `PlayerConfig::preferNativeVideoFrames=false`.
- Session recreates/reopens core player.
- Session seeks to fallback resume position.
- External session event notifies native rendering failure without exposing fallback action to PlayPlugin.
- Repeated fallback for same generation is ignored.

- [ ] **Step 2: Implement fallback handler**

Implementation constraints:

- Fallback must not happen on Qt thread.
- Fallback must cancel old frame acceptance before reopening core.
- Fallback must set runtime timeline before accepting new frames.
- If fallback seek fails, emit `ErrorEvent`.

- [ ] **Step 3: Verify Task 7**

Run:

```bash
cmake --build build --target MediaSdkPlaybackSessionFallbackTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_playback_session_fallback"
```

- [ ] **Step 4: Commit Task 7**

```bash
git add sdk/media_playback_session
git commit -m "[架构新增] 将native fallback收进SDK会话"
```

Stop and ask for confirmation before Task 8.

---

## Task 8: Switch PlayPlugin Adapter To PlaybackSession

**Purpose:** Make PlayPlugin stop owning core/runtime playback orchestration.

**Files:**
- Modify: `plugins/PlayPlugin/CMakeLists.txt`
- Modify: `plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.h`
- Modify: `plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp`
- Modify: `plugins/PlayPlugin/src/playback/PlaybackPipeline.cpp`
- Modify: `tests/playplugin_qt_playback_adapter_checks.py`

- [ ] **Step 1: Add architecture checks**

Update PlayPlugin checks so:

- `SdkPlaybackAdapter` must not inherit `media_sdk::IDecodeFrameSink`.
- `SdkPlaybackAdapter` must not inherit `media_sdk::IEventSink`.
- `SdkPlaybackAdapter` must not inherit `media_sdk::runtime::IRuntimePlayerEvents`.
- `SdkPlaybackAdapter.cpp` must not call `std::make_unique<media_sdk::Player>`.
- `SdkPlaybackAdapter.cpp` must not call `std::make_shared<media_sdk::runtime::RuntimePlayer>`.
- `SdkPlaybackAdapter.cpp` must not contain `float32InterleavedSamples`.

- [ ] **Step 2: Link session module**

Modify `plugins/PlayPlugin/CMakeLists.txt`:

```cmake
target_link_libraries(PlayPlugin PRIVATE
    media_sdk::playback_session
)
```

- [ ] **Step 3: Refactor adapter ownership**

`SdkPlaybackAdapter` should:

- Own `std::unique_ptr<media_sdk::session::PlaybackSession>`.
- Implement `media_sdk::session::ISessionEvents`.
- Forward open/play/pause/seek/stop to session.
- Convert SDK session events to Qt signals.
- Keep Qt generation mapping only for QML seek completion if still needed.
- Stop doing frame push, fallback, audio conversion, runtime creation.

- [ ] **Step 4: Verify Task 8**

Run:

```bash
cmake --build build --target PlayPlugin --parallel
ctest --test-dir build --output-on-failure -R "playplugin_qt_playback_adapter_checks|media_sdk_playback_session"
```

- [ ] **Step 5: Commit Task 8**

```bash
git add plugins/PlayPlugin sdk/media_playback_session tests/playplugin_qt_playback_adapter_checks.py
git commit -m "[架构修改] PlayPlugin切换到SDK播放会话"
```

Stop and ask for confirmation before Task 9.

---

## Task 9: Tighten EOF, Seek, Pause, And Stop Semantics

**Purpose:** Make session-level state transitions deterministic under repeated user operations.

**Files:**
- Modify: `sdk/media_playback_session/src/PlaybackSession.cpp`
- Modify: `sdk/media_playback_session/tests/tst_playback_session_timeline.cpp`
- Modify: `sdk/media_playback_session/tests/tst_playback_session_event_router.cpp`
- Modify: `plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp` only if Qt signal mapping needs adjustment.

- [ ] **Step 1: Add stress-style deterministic tests**

Test cases:

- seek while playing cancels old frame push and accepts only new generation.
- seek while paused keeps session paused after seek completion.
- stop during fallback does not emit stale EOF or seek completion.
- open another file after a previous file stops does not accept old frames.
- core EOF before video presented does not emit external EOF until runtime EOF.

- [ ] **Step 2: Fix state transitions**

Fix only behavior covered by failing tests. Do not refactor unrelated runtime internals.

- [ ] **Step 3: Verify Task 9**

Run:

```bash
cmake --build build --target MediaSdkPlaybackSessionTimelineTest MediaSdkPlaybackSessionEventRouterTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_playback_session"
```

- [ ] **Step 4: Commit Task 9**

```bash
git add sdk/media_playback_session plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp
git commit -m "[稳定性修复] 收紧SDK会话seek和EOF语义"
```

Stop and ask for confirmation before Task 10.

---

## Task 10: Diagnostics And Performance Guardrails

**Purpose:** Preserve performance and make regressions observable.

**Files:**
- Modify: `sdk/media_playback_session/include/media_sdk/session/PlaybackSessionTypes.h`
- Modify: `sdk/media_playback_session/src/PlaybackSession.cpp`
- Modify: `plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp`
- Modify: existing architecture/perf logging checks if present.

- [ ] **Step 1: Add diagnostics forwarding tests**

Test cases:

- `PlaybackSession::diagnostics()` returns runtime diagnostics.
- fallback count is visible.
- queue backpressure wait time is visible.
- native/cpu presented counts are visible.

- [ ] **Step 2: Add PlayPlugin logging bridge**

PlayPlugin may log session diagnostics, but must not compute playback policy from them.

- [ ] **Step 3: Verify Task 10**

Run:

```bash
cmake --build build --target PlayPlugin MediaSdkPlaybackSessionContractTest --parallel
ctest --test-dir build --output-on-failure -R "media_sdk_playback_session|playplugin"
```

- [ ] **Step 4: Commit Task 10**

```bash
git add sdk/media_playback_session plugins/PlayPlugin
git commit -m "[可观测性] 增加SDK会话诊断输出"
```

Stop and ask for confirmation before Task 11.

---

## Task 11: Remove PlayPlugin Session-Orchestration Dead Code

**Purpose:** Clean up only code that is proven unused after PlayPlugin switches to `PlaybackSession`.

**Files:**
- Modify: `plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.h/.cpp`
- Evaluate: `plugins/PlayPlugin/src/audio/AudioRenderer.*`
- Evaluate: `plugins/PlayPlugin/src/video/VideoRenderer.*`
- Evaluate: `plugins/PlayPlugin/src/video/VideoFrameScheduler.*`
- Evaluate: `plugins/PlayPlugin/src/sync/ClockSync.h`
- Modify: `plugins/PlayPlugin/CMakeLists.txt` only for files no longer referenced.

- [ ] **Step 1: Use `rg` to prove usage**

Run:

```bash
rg -n "AudioRenderer|VideoRenderer|VideoFrameScheduler|ClockSync|PlaybackDataBridge|QtPlaybackAdapter" plugins/PlayPlugin sdk tests
```

Document each removal candidate before deletion.

- [ ] **Step 2: Remove only unused orchestration code**

Do not delete Qt presenter, FFmpegSurface, VideoNode, VideoMaterial, or native texture bridge.

- [ ] **Step 3: Verify Task 11**

Run:

```bash
cmake --build build --target PlayPlugin --parallel
ctest --test-dir build --output-on-failure -R "playplugin"
```

- [ ] **Step 4: Commit Task 11**

```bash
git add plugins/PlayPlugin tests
git commit -m "[架构清理] 移除PlayPlugin播放编排残留"
```

Stop and ask for confirmation before Task 12.

---

## Task 12: Final Full Verification And Manual Test Checklist

**Purpose:** Validate C stage first slice end-to-end.

**Files:**
- Modify: docs only if verification discovers missing constraints.

- [ ] **Step 1: Run full build**

```bash
cmake --build build --parallel
```

Expected: success.

- [ ] **Step 2: Run full CTest**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 100% tests passed.

- [ ] **Step 3: Manual playback checklist**

Run app:

```bash
./build/app/PluginBasedApp
```

Manual checks:

- Open `/Users/zs/Downloads/6月29日.mov`.
- 4K 60fps playback has no obvious regression versus B+ baseline.
- Seek randomly more than 10 times; video resumes and does not freeze.
- Pause/resume repeatedly; no deadlock.
- Open file A, then open file B; no stale frame rejection error reaches UI.
- Native path diagnostics show native presented when supported.
- Disable or fail native path; fallback to CPU path does not stop playback.
- EOF reaches UI only after runtime drain.

- [ ] **Step 4: Commit docs/verification adjustment if needed**

If only docs/checklist changed:

```bash
git add docs
git commit -m "[文档] 更新C阶段验收记录"
```

Stop and report final status.

---

## Phase Boundaries

Recommended user confirmation points:

- After Task 1: module skeleton compiles.
- After Task 4: frame routing is SDK-side but PlayPlugin unchanged.
- After Task 7: fallback logic is SDK-side.
- After Task 8: PlayPlugin uses `PlaybackSession`.
- After Task 12: full CI/manual verification complete.

## Rollback Strategy

- Tasks 1-7 are additive to SDK and should not alter PlayPlugin behavior.
- Task 8 is the first behavior switch. If it fails, revert only Task 8 and keep SDK session module for further tests.
- Task 11 cleanup must not run until Task 8-10 are stable and manually verified.

## Open Questions Before Implementation

1. `PlaybackSession` concrete audio output ownership: keep PlayPlugin ownership for C first slice, or move platform factory into SDK now?
   - Recommendation: keep PlayPlugin ownership first; move factory later.
2. No-audio media policy: skip audio output entirely or open silent clock?
   - Recommendation: SDK session should skip audio output and make runtime use video/self clock.
3. External EOF semantics: should UI receive core decode EOF and runtime drained EOF separately?
   - Recommendation: UI receives only runtime drained EOF; diagnostics may expose core EOF.
4. ABI policy: source-only C++20 API for now or stable ABI wrapper?
   - Recommendation: source-only C++20 for this stage; C ABI wrapper is a later packaging project.
