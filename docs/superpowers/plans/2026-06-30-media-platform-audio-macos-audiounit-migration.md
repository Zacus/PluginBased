# media_platform_audio_macos AudioUnit Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the macOS SDK audio output implementation from AudioQueue to AudioUnit while preserving `CoreAudioAudioOutput` and `IAudioOutput` contracts.

**Architecture:** Keep the public `CoreAudioAudioOutput` adapter stable and move device-specific work behind an internal render-device interface. Reuse `CoreAudioRingBuffer` for SPSC PCM buffering and clock snapshots; AudioUnit render callback only reads PCM/silence and never touches locks, allocation, logging, Qt, or lifecycle calls.

**Tech Stack:** C++20, CMake, CoreAudio, AudioUnit, `media_sdk_playback_runtime::IAudioOutput`, Python architecture checks, CTest.

---

## Design Reference

- Design doc: `docs/superpowers/specs/2026-06-30-media-platform-audio-macos-audiounit-migration-design.md`
- Current public adapter: `sdk/media_platform_audio_macos/include/media_sdk/platform/macos/CoreAudioAudioOutput.h`
- Current implementation: `sdk/media_platform_audio_macos/src/CoreAudioAudioOutput.cpp`
- Current ring buffer: `sdk/media_platform_audio_macos/src/CoreAudioRingBuffer.h`
- Current tests: `sdk/media_platform_audio_macos/tests/tst_core_audio_output_contract.cpp`, `sdk/media_platform_audio_macos/tests/tst_core_audio_ring_buffer.cpp`
- Architecture checks: `tests/media_sdk_playback_runtime_architecture_checks.py`

## File Structure

- Modify `sdk/media_platform_audio_macos/include/media_sdk/platform/macos/CoreAudioAudioOutput.h`
  - Keep public constructor, destructor, deleted copy, and `IAudioOutput` overrides unchanged.
  - Keep AudioUnit/CoreAudio headers out of public include.

- Create `sdk/media_platform_audio_macos/src/AudioRenderDevice.h`
  - Internal device interface for output lifecycle.
  - Defines callback function pointer/context, device format, and diagnostics structs.

- Create `sdk/media_platform_audio_macos/src/CoreAudioOutputEngine.h`
  - Internal engine owning `CoreAudioRingBuffer` and an injected `IAudioRenderDevice`.
  - Exposes methods matching `IAudioOutput`.

- Create `sdk/media_platform_audio_macos/src/CoreAudioOutputEngine.cpp`
  - Implements format validation, state machine, generation, clock pause flag, and device lifecycle.
  - Contains static render callback bridge that calls ring-buffer `read()`.

- Create `sdk/media_platform_audio_macos/src/MacAudioUnitRenderDevice.h`
  - Internal factory declaration for the real AudioUnit render device.

- Create `sdk/media_platform_audio_macos/src/MacAudioUnitRenderDevice.cpp`
  - Owns `AudioUnit`.
  - Creates default output unit, sets stream format and render callback, initializes, starts, stops, resets, closes.

- Modify `sdk/media_platform_audio_macos/src/CoreAudioAudioOutput.cpp`
  - Reduce to pimpl adapter that constructs `CoreAudioOutputEngine` with `makeMacAudioUnitRenderDevice()`.

- Modify `sdk/media_platform_audio_macos/src/CoreAudioRingBuffer.h`
  - Preserve SPSC behavior and realtime-safe `read()`.
  - Replace fixed producer polling with deterministic wake semantics if tests prove the current 1ms wait is still required.

- Create `sdk/media_platform_audio_macos/tests/tst_core_audio_output_engine.cpp`
  - Fake render device tests for lifecycle and callback consumption.

- Modify `sdk/media_platform_audio_macos/tests/tst_core_audio_output_contract.cpp`
  - Keep public adapter contract and make real-device availability explicit.

- Modify `sdk/media_platform_audio_macos/CMakeLists.txt`
  - Add new source files and tests.
  - Link AudioUnit framework.
  - Remove AudioQueue-specific framework dependency if no longer needed by real source.

- Modify `tests/media_sdk_playback_runtime_architecture_checks.py`
  - Require AudioUnit implementation tokens.
  - Forbid AudioQueue tokens in `sdk/media_platform_audio_macos`.

## Task 1: Architecture Guardrails

**Files:**
- Modify: `tests/media_sdk_playback_runtime_architecture_checks.py`
- Test: `tests/media_sdk_playback_runtime_architecture_checks.py`

- [ ] **Step 1: Add AudioQueue forbiddance**

Add a platform-audio scan that fails on:

```python
for token in (
    "AudioQueue",
    "AudioQueueRef",
    "AudioQueueBufferRef",
    "AudioQueueNewOutput",
    "AudioQueueEnqueueBuffer",
    "AudioQueueStart",
    "AudioQueuePause",
    "AudioQueueReset",
    "AudioQueueStop",
    "AudioQueueDispose",
):
    if token in platform_audio_source:
        raise AssertionError(f"{path} must not use AudioQueue after AudioUnit migration, found {token!r}")
```

- [ ] **Step 2: Add AudioUnit requirement**

Require `sdk/media_platform_audio_macos/src/MacAudioUnitRenderDevice.cpp` to contain:

```python
for token in (
    "AudioComponentFindNext",
    "AudioComponentInstanceNew",
    "AudioUnitSetProperty",
    "kAudioUnitSubType_DefaultOutput",
    "kAudioUnitProperty_StreamFormat",
    "kAudioUnitProperty_SetRenderCallback",
    "AudioUnitInitialize",
    "AudioOutputUnitStart",
    "AudioOutputUnitStop",
    "AudioUnitReset",
    "AudioComponentInstanceDispose",
):
    assert_contains(audio_unit_device_source, token, audio_unit_device_path)
```

- [ ] **Step 3: Preserve public header boundary**

Extend the existing concrete header check so `CoreAudioAudioOutput.h` fails if it contains:

```python
for token in (
    "<AudioUnit/",
    "<AudioToolbox/",
    "<CoreAudio/",
    "AudioUnit",
    "AudioQueue",
):
    if token in concrete_audio_output:
        raise AssertionError(f"{concrete_header} must not expose macOS audio implementation token {token!r}")
```

- [ ] **Step 4: Run focused check and confirm it fails before migration**

Run:

```bash
ctest --test-dir build --output-on-failure -R media_sdk_playback_runtime_architecture_checks
```

Expected: FAIL because `CoreAudioAudioOutput.cpp` still contains AudioQueue tokens and `MacAudioUnitRenderDevice.cpp` does not exist.

- [ ] **Step 5: Commit**

```bash
git add tests/media_sdk_playback_runtime_architecture_checks.py
git commit -m "[架构约束] 增加macOS音频AudioUnit迁移检查"
```

## Task 2: Internal Render Device Interface

**Files:**
- Create: `sdk/media_platform_audio_macos/src/AudioRenderDevice.h`
- Create: `sdk/media_platform_audio_macos/src/CoreAudioOutputEngine.h`
- Create: `sdk/media_platform_audio_macos/src/CoreAudioOutputEngine.cpp`
- Create: `sdk/media_platform_audio_macos/tests/tst_core_audio_output_engine.cpp`
- Modify: `sdk/media_platform_audio_macos/CMakeLists.txt`

- [ ] **Step 1: Define internal device interface**

Create `AudioRenderDevice.h` with:

```cpp
#pragma once

#include "media_sdk/Result.h"
#include "media_sdk/runtime/AudioOutput.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace media_sdk::platform::macos {

struct AudioRenderCallback {
    using Function = void (*)(void* context, std::span<std::byte> destination) noexcept;

    Function function = nullptr;
    void* context = nullptr;
};

struct AudioRenderDeviceConfig {
    runtime::AudioFormat format {};
    AudioRenderCallback callback {};
};

struct AudioRenderDeviceDiagnostics {
    std::uint64_t startFailures = 0;
    std::uint64_t stopFailures = 0;
    std::uint64_t resetFailures = 0;
};

class IAudioRenderDevice {
public:
    virtual ~IAudioRenderDevice() = default;

    [[nodiscard("open failures must leave the audio output closed")]]
    virtual Result<void> open(const AudioRenderDeviceConfig& config) = 0;
    [[nodiscard("start failures affect audio clock validity and playback state")]]
    virtual Result<void> start() = 0;
    virtual void stop() noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual void close() noexcept = 0;
    [[nodiscard("hardware latency is part of A/V sync diagnostics")]]
    virtual std::chrono::microseconds hardwareLatency() const noexcept = 0;
    [[nodiscard("diagnostics are used by platform-audio tests")]]
    virtual AudioRenderDeviceDiagnostics diagnostics() const noexcept = 0;
};

} // namespace media_sdk::platform::macos
```

- [ ] **Step 2: Add engine skeleton**

Create `CoreAudioOutputEngine.h` declaring:

```cpp
#pragma once

#include "AudioRenderDevice.h"
#include "CoreAudioRingBuffer.h"

#include <memory>
#include <mutex>

namespace media_sdk::platform::macos {

class CoreAudioOutputEngine final {
public:
    explicit CoreAudioOutputEngine(std::unique_ptr<IAudioRenderDevice> device);
    ~CoreAudioOutputEngine();

    CoreAudioOutputEngine(const CoreAudioOutputEngine&) = delete;
    CoreAudioOutputEngine& operator=(const CoreAudioOutputEngine&) = delete;

    [[nodiscard("inspect the CoreAudio open result before writing audio")]]
    Result<void> open(const runtime::AudioFormat& format);
    [[nodiscard("CoreAudio writes can reject stale generations or closed outputs")]]
    Result<void> write(runtime::AudioBufferView buffer);
    [[nodiscard("CoreAudio clock snapshots drive runtime A/V sync")]]
    runtime::ClockSnapshot clock() const;
    void pause();
    void resume();
    void flush();
    void close();

private:
    static void renderCallback(void* context, std::span<std::byte> destination) noexcept;
    void render(std::span<std::byte> destination) noexcept;

    mutable std::mutex m_mutex;
    std::unique_ptr<IAudioRenderDevice> m_device;
    CoreAudioRingBuffer m_ringBuffer;
    runtime::AudioFormat m_format {};
    runtime::Generation m_generation = 1;
    bool m_open = false;
    bool m_paused = false;
    bool m_running = false;
};

} // namespace media_sdk::platform::macos
```

- [ ] **Step 3: Add fake-device tests first**

Create `tst_core_audio_output_engine.cpp` with a fake `IAudioRenderDevice` that records `open/start/stop/reset/close`, stores `AudioRenderCallback`, and exposes a `render(std::span<std::byte>)` method that invokes the callback.

Test cases:

```cpp
void resumeStartsDeviceAndCallbackConsumesPcm();
void pauseStopsDeviceWithoutFlushingQueuedAudio();
void flushResetsDeviceAndRejectsOldGeneration();
void closeStopsAndRejectsWrites();
```

Use `assert()` and the same style as existing platform tests.

- [ ] **Step 4: Add test target**

Add to `sdk/media_platform_audio_macos/CMakeLists.txt`:

```cmake
add_executable(MediaSdkPlatformAudioMacosEngineTest
    tests/tst_core_audio_output_engine.cpp
    src/CoreAudioOutputEngine.cpp
)
target_compile_features(MediaSdkPlatformAudioMacosEngineTest PRIVATE cxx_std_20)
set_target_properties(MediaSdkPlatformAudioMacosEngineTest PROPERTIES
    CXX_EXTENSIONS OFF
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_include_directories(MediaSdkPlatformAudioMacosEngineTest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(MediaSdkPlatformAudioMacosEngineTest PRIVATE
    media_sdk::playback_runtime
)
add_test(NAME media_sdk_platform_audio_macos_engine
    COMMAND $<TARGET_FILE:MediaSdkPlatformAudioMacosEngineTest>
)
```

- [ ] **Step 5: Run engine test and confirm it fails before implementation**

Run:

```bash
cmake --build build --target MediaSdkPlatformAudioMacosEngineTest --parallel
ctest --test-dir build --output-on-failure -R media_sdk_platform_audio_macos_engine
```

Expected: build or test FAIL until `CoreAudioOutputEngine.cpp` implements the skeleton.

- [ ] **Step 6: Commit**

```bash
git add sdk/media_platform_audio_macos
git commit -m "[功能新增] 抽象macOS音频AudioUnit设备接口"
```

## Task 3: CoreAudioOutputEngine Implementation

**Files:**
- Modify: `sdk/media_platform_audio_macos/src/CoreAudioOutputEngine.cpp`
- Modify: `sdk/media_platform_audio_macos/tests/tst_core_audio_output_engine.cpp`

- [ ] **Step 1: Implement validation and open**

`open()` must:

- reject `sampleRate <= 0`
- reject `channels <= 0`
- reject non-`Float32`
- call `close()` first
- configure ring buffer with generation 1
- call device `open()` with `AudioRenderCallback { &CoreAudioOutputEngine::renderCallback, this }`
- on device open failure, close ring buffer and leave `m_open == false`

- [ ] **Step 2: Implement write and callback render**

`write()` must reject closed output and delegate to `CoreAudioRingBuffer::write()`.

`renderCallback()` must be `noexcept`, null-check context, and call:

```cpp
static_cast<CoreAudioOutputEngine*>(context)->render(destination);
```

`render()` must only call:

```cpp
(void)m_ringBuffer.read(destination);
```

No lock, allocation, logging, or device calls are allowed in `render()`.

- [ ] **Step 3: Implement pause/resume/flush/close**

Rules:

- `resume()` starts device only when open and not running.
- `pause()` stops device only when running; it must not flush ring buffer.
- `flush()` increments generation through `m_ringBuffer.flush()` and calls `m_device->reset()`.
- `close()` stops/closes device, sets state closed, and calls `m_ringBuffer.close()`.

- [ ] **Step 4: Implement clock**

`clock()` must get ring clock, then override:

```cpp
snapshot.paused = m_paused;
snapshot.hardwareLatency = m_device ? m_device->hardwareLatency() : 0us;
```

If output is closed, `snapshot.valid` must be false.

- [ ] **Step 5: Run tests**

Run:

```bash
cmake --build build --target MediaSdkPlatformAudioMacosEngineTest --parallel
ctest --test-dir build --output-on-failure -R media_sdk_platform_audio_macos_engine
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add sdk/media_platform_audio_macos/src/CoreAudioOutputEngine.cpp sdk/media_platform_audio_macos/tests/tst_core_audio_output_engine.cpp
git commit -m "[功能新增] 实现macOS音频输出引擎状态机"
```

## Task 4: AudioUnit Render Device

**Files:**
- Create: `sdk/media_platform_audio_macos/src/MacAudioUnitRenderDevice.h`
- Create: `sdk/media_platform_audio_macos/src/MacAudioUnitRenderDevice.cpp`
- Modify: `sdk/media_platform_audio_macos/CMakeLists.txt`

- [ ] **Step 1: Add factory header**

Create `MacAudioUnitRenderDevice.h`:

```cpp
#pragma once

#include "AudioRenderDevice.h"

#include <memory>

namespace media_sdk::platform::macos {

[[nodiscard("CoreAudioAudioOutput requires a concrete macOS render device")]]
std::unique_ptr<IAudioRenderDevice> makeMacAudioUnitRenderDevice();

} // namespace media_sdk::platform::macos
```

- [ ] **Step 2: Implement RAII AudioUnit ownership**

`MacAudioUnitRenderDevice.cpp` must:

- include `<AudioUnit/AudioUnit.h>`
- keep `AudioUnit m_unit = nullptr`
- call `AudioOutputUnitStop` before dispose when initialized/running
- call `AudioUnitUninitialize` before dispose when initialized
- call `AudioComponentInstanceDispose`
- set `m_unit = nullptr` in `close()`

- [ ] **Step 3: Implement callback bridge**

The AudioUnit callback must:

- be `noexcept` in behavior even though the C signature cannot express it
- convert `ioData->mBuffers[0].mData` and `mDataByteSize` to `std::span<std::byte>`
- fill silence if callback/config/context is invalid
- call `config.callback.function(config.callback.context, destination)`
- never log or allocate

- [ ] **Step 4: Implement open/start/stop/reset**

`open()` must:

- close previous unit
- find `kAudioUnitSubType_DefaultOutput`
- create component instance
- set interleaved Float32 ASBD
- set render callback
- initialize unit
- query `kAudioUnitProperty_Latency`

`start()` must call `AudioOutputUnitStart`.

`stop()` must call `AudioOutputUnitStop` only when running.

`reset()` must call `AudioUnitReset(m_unit, kAudioUnitScope_Global, 0)` when open.

- [ ] **Step 5: Update CMake**

Add sources:

```cmake
src/CoreAudioOutputEngine.cpp
src/MacAudioUnitRenderDevice.cpp
```

Link frameworks:

```cmake
target_link_libraries(media_sdk_platform_audio_macos PRIVATE
    "-framework CoreAudio"
    "-framework AudioUnit"
)
```

Keep `"-framework AudioToolbox"` only if the compiler/linker requires `AudioComponent` symbols on the supported macOS SDK.

- [ ] **Step 6: Build platform target**

Run:

```bash
cmake --build build --target media_sdk_platform_audio_macos --parallel
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add sdk/media_platform_audio_macos
git commit -m "[功能新增] 实现macOS AudioUnit音频设备"
```

## Task 5: Wire Public Adapter To AudioUnit Engine

**Files:**
- Modify: `sdk/media_platform_audio_macos/src/CoreAudioAudioOutput.cpp`
- Modify: `sdk/media_platform_audio_macos/tests/tst_core_audio_output_contract.cpp`

- [ ] **Step 1: Replace AudioQueue implementation**

`CoreAudioAudioOutput.cpp` should:

- include `CoreAudioOutputEngine.h`
- include `MacAudioUnitRenderDevice.h`
- remove all AudioQueue headers and tokens
- store `std::unique_ptr<CoreAudioOutputEngine>` in `Impl`
- forward each public method to engine

- [ ] **Step 2: Keep public behavior**

Existing behavior must remain:

- invalid open fails
- closed write fails
- valid open sets `clock().valid == true`
- `write()` accepts current generation
- `flush()` increments generation
- old generation write fails
- `close()` invalidates clock

- [ ] **Step 3: Make real-device availability explicit**

If default output AudioUnit is unavailable in CI/headless environments, the contract test must print a clear skip line and return success only for the real-device section. Invalid-format and closed-write tests must still execute.

- [ ] **Step 4: Run tests**

Run:

```bash
cmake --build build --target MediaSdkPlatformAudioMacosContractTest MediaSdkPlatformAudioMacosEngineTest --parallel
ctest --test-dir build --output-on-failure -R 'media_sdk_platform_audio_macos_contract|media_sdk_platform_audio_macos_engine'
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add sdk/media_platform_audio_macos
git commit -m "[功能修改] 将CoreAudioAudioOutput切换到AudioUnit"
```

## Task 6: Ring Buffer Backpressure Cleanup

**Files:**
- Modify: `sdk/media_platform_audio_macos/src/CoreAudioRingBuffer.h`
- Modify: `sdk/media_platform_audio_macos/tests/tst_core_audio_ring_buffer.cpp`
- Modify: `tests/media_sdk_playback_runtime_architecture_checks.py`

- [ ] **Step 1: Add failing test for deterministic producer wake**

Add a test that:

- fills a small ring buffer
- starts an async writer blocked by capacity
- calls `read()` once from the consumer side
- verifies the writer completes without relying on repeated fixed sleeps

The expected behavior is:

```cpp
assert(result.wait_for(1s) == std::future_status::ready);
assert(result.get());
```

- [ ] **Step 2: Replace fixed polling if still present**

If `CoreAudioRingBuffer::write()` still contains:

```cpp
m_notFull.wait_for(lock, std::chrono::milliseconds { 1 }, ...)
```

replace it with a deterministic wake model. The preferred C++20 model is cursor-based `std::atomic::wait/notify_one` or a non-realtime side-channel that does not require mutex/condition-variable calls from the AudioUnit callback. Do not add a mutex to `read()`.

- [ ] **Step 3: Keep callback path realtime-safe**

Architecture check must still fail if `read()` contains:

```python
"std::scoped_lock",
"std::unique_lock",
".lock(",
".wait(",
".notify_",
"m_mutex",
```

- [ ] **Step 4: Run ring tests**

Run:

```bash
cmake --build build --target MediaSdkPlatformAudioMacosRingBufferTest --parallel
ctest --test-dir build --output-on-failure -R media_sdk_platform_audio_macos_ring_buffer
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add sdk/media_platform_audio_macos/src/CoreAudioRingBuffer.h sdk/media_platform_audio_macos/tests/tst_core_audio_ring_buffer.cpp tests/media_sdk_playback_runtime_architecture_checks.py
git commit -m "[性能优化] 收口CoreAudioRingBuffer生产者唤醒"
```

## Task 7: Architecture And Full Verification

**Files:**
- Modify: `tests/media_sdk_playback_runtime_architecture_checks.py`
- Modify: `sdk/media_platform_audio_macos/CMakeLists.txt`

- [ ] **Step 1: Run AudioQueue scan**

Run:

```bash
rg "AudioQueue|AudioQueueRef|AudioQueueNewOutput|AudioQueueEnqueueBuffer|AudioQueueStart" sdk/media_platform_audio_macos -n
```

Expected: no source hits. Hits inside design documents are acceptable only outside `sdk/media_platform_audio_macos`.

- [ ] **Step 2: Run architecture checks**

Run:

```bash
ctest --test-dir build --output-on-failure -R media_sdk_playback_runtime_architecture_checks
```

Expected: PASS.

- [ ] **Step 3: Run platform audio tests**

Run:

```bash
ctest --test-dir build --output-on-failure -R media_sdk_platform_audio_macos
```

Expected: PASS.

- [ ] **Step 4: Run full build and test suite**

Run:

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit final cleanup**

```bash
git add sdk/media_platform_audio_macos tests/media_sdk_playback_runtime_architecture_checks.py
git commit -m "[质量优化] 完成macOS音频AudioUnit迁移验收"
```

## Task 8: Manual Playback Verification

**Files:**
- No source changes expected unless verification finds a defect.

- [ ] **Step 1: Launch app**

Run:

```bash
./build/app/PluginBasedApp
```

- [ ] **Step 2: Verify sample playback**

Use `/Users/zs/Downloads/6月29日.mov`.

Checks:

- video has image from start
- audio is normal
- random seek 10 times, including close positions
- pause/resume 10 times
- playback does not auto-interrupt
- no long frozen period after seek
- PlayPerf keeps reporting rendered frames and `audio_clock=true`

- [ ] **Step 3: Commit defect fixes separately**

If manual verification finds a defect, fix and commit each defect separately:

```bash
git add <changed-files>
git commit -m "[功能修复] 修复AudioUnit迁移后的<具体问题>"
```

## Self-Review

- Spec coverage: AudioQueue removal, AudioUnit default output, realtime callback constraints, lifecycle, clock/latency, tests, and manual playback are each covered by tasks.
- Placeholder scan: no task relies on unspecified files or unspecified commands.
- Type consistency: internal types are consistently named `IAudioRenderDevice`, `AudioRenderDeviceConfig`, `AudioRenderCallback`, `CoreAudioOutputEngine`, and `MacAudioUnitRenderDevice`.

