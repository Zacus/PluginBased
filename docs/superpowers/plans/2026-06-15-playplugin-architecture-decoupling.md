# PlayPlugin Architecture Decoupling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce `PlaybackPipeline` so `PlayerEngine` becomes a QML-facing facade instead of directly owning decoder, renderer, queues, and clock. Later phases also reduce `FFmpegSurface` and `FFmpegDecoder` into facades around focused internal helpers.

**Architecture:** Keep public QML API stable. Add an internal QObject `PlaybackPipeline` that owns low-level playback components, forwards their signals, and provides component lifecycle commands. Keep completion state in `PlayerEngine` for this phase to minimize behavior risk. For lower-level decoder/surface refactors, prefer small value helpers and structured results over broad inheritance or pimpl.

**Tech Stack:** C++17, Qt 6 Core/Quick/Multimedia, QThread, QPointer, FFmpeg, CMake, Python architecture checks.

---

## File Map

- Create `plugins/PlayPlugin/src/PlaybackPipeline.h`: internal pipeline API, owned playback components, forwarded signals.
- Create `plugins/PlayPlugin/src/PlaybackPipeline.cpp`: component construction, signal wiring, stop/seek/open/surface/native rendering logic.
- Modify `plugins/PlayPlugin/src/PlayerEngine.h`: replace direct low-level members with `std::unique_ptr<PlaybackPipeline>`.
- Modify `plugins/PlayPlugin/src/PlayerEngine.cpp`: delegate low-level commands to the pipeline and connect pipeline signals.
- Modify `plugins/PlayPlugin/CMakeLists.txt`: compile the new pipeline files.
- Create `tests/playplugin_architecture_decoupling_checks.py`: static checks for the new boundary.
- Modify `CMakeLists.txt`: add the architecture check to CTest.

Additional completed helper boundaries:

- `plugins/PlayPlugin/src/render/VideoPixelFormat.h/.cpp`: video pixel format mapping.
- `plugins/PlayPlugin/src/render/VideoMaterial.h/.cpp`: RHI material, shader, texture upload, and software YUV binding.
- `plugins/PlayPlugin/src/render/VideoNode.h/.cpp`: scene graph node and native texture lifecycle.
- `plugins/PlayPlugin/src/render/VideoSurfaceGeometry.h/.cpp`: aspect-preserving draw rectangle calculation.
- `plugins/PlayPlugin/src/decode/DecodePerformance.h/.cpp`: decode performance counters and throttled logs.
- `plugins/PlayPlugin/src/decode/VideoFrameProcessor.h/.cpp`: hardware frame transfer and video pixel normalization.
- `plugins/PlayPlugin/src/decode/MediaOpener.h/.cpp`: input open, stream discovery, codec setup, and hardware backend selection.
- `plugins/PlayPlugin/src/decode/StreamFrameDecoder.h/.cpp`: packet send, frame receive, and PTS normalization.
- `plugins/PlayPlugin/src/decode/FFmpegLogBridge.h/.cpp`: FFmpeg global log callback bridge.

## Task 1: Add Architecture Guard

**Files:**
- Create: `tests/playplugin_architecture_decoupling_checks.py`
- Modify: `CMakeLists.txt`

- [x] **Step 1: Write the failing architecture test**

Create `tests/playplugin_architecture_decoupling_checks.py`:

```python
#!/usr/bin/env python3

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    pipeline_h_path = ROOT / "plugins/PlayPlugin/src/PlaybackPipeline.h"
    pipeline_cpp_path = ROOT / "plugins/PlayPlugin/src/PlaybackPipeline.cpp"
    require(pipeline_h_path.exists(), "PlaybackPipeline.h should exist")
    require(pipeline_cpp_path.exists(), "PlaybackPipeline.cpp should exist")

    engine_h = read("plugins/PlayPlugin/src/PlayerEngine.h")
    engine_cpp = read("plugins/PlayPlugin/src/PlayerEngine.cpp")
    pipeline_h = read("plugins/PlayPlugin/src/PlaybackPipeline.h")
    pipeline_cpp = read("plugins/PlayPlugin/src/PlaybackPipeline.cpp")
    cmake = read("plugins/PlayPlugin/CMakeLists.txt")
    root_cmake = read("CMakeLists.txt")

    require("class PlaybackPipeline" in pipeline_h, "PlaybackPipeline class should be declared")
    require("std::unique_ptr<PlaybackPipeline> m_pipeline" in engine_h,
            "PlayerEngine should own a PlaybackPipeline")
    for forbidden in (
        "VideoFrameQueue m_videoQueue",
        "AudioFrameQueue m_audioQueue",
        "ClockSync m_clock",
        "std::unique_ptr<FFmpegDecoder>",
        "std::unique_ptr<AudioRenderer>",
        "std::unique_ptr<VideoRenderer>",
    ):
        require(forbidden not in engine_h,
                f"PlayerEngine should not directly own low-level playback member: {forbidden}")

    require("FFmpegDecoder" in pipeline_h and "AudioRenderer" in pipeline_h and "VideoRenderer" in pipeline_h,
            "PlaybackPipeline should own decoder and renderers")
    require("void stopComponents()" in pipeline_h,
            "PlaybackPipeline should expose deterministic component shutdown")
    require("void seek(qint64 positionMs, int generation)" in pipeline_h,
            "PlaybackPipeline should coordinate seek requests")
    require("void setSurface(FFmpegSurface* surface)" in pipeline_h,
            "PlaybackPipeline should own surface signal binding")
    require("nativeRenderingFailed" in pipeline_h and "nativeRenderingFailed" in pipeline_cpp,
            "PlaybackPipeline should forward native rendering failure")
    require("m_pipeline->" in engine_cpp,
            "PlayerEngine should delegate low-level operations to PlaybackPipeline")
    require("PlaybackPipeline.h" in cmake and "PlaybackPipeline.cpp" in cmake,
            "PlayPlugin CMake should compile PlaybackPipeline")
    require("playplugin_architecture_decoupling_checks" in root_cmake,
            "CTest should run the architecture decoupling check")


if __name__ == "__main__":
    main()
```

Add this test to the top-level `BUILD_TESTING` block in `CMakeLists.txt`:

```cmake
    add_test(NAME playplugin_architecture_decoupling_checks
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/playplugin_architecture_decoupling_checks.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
```

- [x] **Step 2: Run the test and verify RED**

Run:

```bash
python3 tests/playplugin_architecture_decoupling_checks.py
```

Expected: FAIL with `PlaybackPipeline.h should exist`.

## Task 2: Introduce PlaybackPipeline

**Files:**
- Create: `plugins/PlayPlugin/src/PlaybackPipeline.h`
- Create: `plugins/PlayPlugin/src/PlaybackPipeline.cpp`
- Modify: `plugins/PlayPlugin/CMakeLists.txt`

- [x] **Step 1: Add PlaybackPipeline header and implementation**

Create `PlaybackPipeline` as a QObject that owns queues, clock, decoder, audio renderer, and video renderer. It must expose the commands and signals described in the design document.

- [x] **Step 2: Add files to CMake**

Add `src/PlaybackPipeline.h src/PlaybackPipeline.cpp` to the `qt_add_qml_module(PlayPlugin SOURCES ...)` list near `PlayerEngine`.

- [x] **Step 3: Run the architecture test**

Run:

```bash
python3 tests/playplugin_architecture_decoupling_checks.py
```

Expected: still FAIL because `PlayerEngine` has not delegated yet.

## Task 3: Delegate PlayerEngine To PlaybackPipeline

**Files:**
- Modify: `plugins/PlayPlugin/src/PlayerEngine.h`
- Modify: `plugins/PlayPlugin/src/PlayerEngine.cpp`

- [x] **Step 1: Replace direct members**

In `PlayerEngine.h`, forward declare `PlaybackPipeline`, remove direct includes for low-level playback classes, and replace direct low-level members with:

```cpp
std::unique_ptr<PlaybackPipeline> m_pipeline;
```

- [x] **Step 2: Wire pipeline signals**

In the constructor, create `m_pipeline` and connect pipeline signals to existing `PlayerEngine` slots.

- [x] **Step 3: Delegate commands**

Replace direct decoder/renderer/queue/surface operations with equivalent `m_pipeline` calls.

- [x] **Step 4: Run the architecture test**

Run:

```bash
python3 tests/playplugin_architecture_decoupling_checks.py
```

Expected: PASS with no output.

## Task 4: Verify Existing Regression Coverage

**Files:**
- No source edits expected.

- [x] **Step 1: Run static PlayPlugin regression checks**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: PASS with no output.

- [x] **Step 2: Run CTest if a configured build exists**

Run:

```bash
ctest --test-dir build --output-on-failure
```

Expected: all configured tests pass. If `build` is not configured locally, configure/build first using the repository build instructions.

- [x] **Step 3: Build**

Run:

```bash
cmake --build build --parallel
```

Expected: `PlayPlugin` and `PluginBasedApp` build successfully.

## Task 5: Completed Surface Helper Extraction

**Files:**
- Created: `plugins/PlayPlugin/src/render/VideoPixelFormat.h/.cpp`
- Created: `plugins/PlayPlugin/src/render/VideoMaterial.h/.cpp`
- Created: `plugins/PlayPlugin/src/render/VideoNode.h/.cpp`
- Created: `plugins/PlayPlugin/src/render/VideoSurfaceGeometry.h/.cpp`
- Modified: `plugins/PlayPlugin/src/FFmpegSurface.h/.cpp`
- Modified: `plugins/PlayPlugin/CMakeLists.txt`
- Created/updated: `tests/playplugin_surface_decoupling_checks.py`

- [x] **Step 1: Extract video pixel format mapping**
- [x] **Step 2: Extract render material and shader upload logic**
- [x] **Step 3: Extract scene graph node and native texture lifecycle**
- [x] **Step 4: Extract draw rectangle geometry calculation**
- [x] **Step 5: Update CMake and architecture checks**
- [x] **Step 6: Build and run CTest**

Commits:

- `73ce38a [功能修改] 抽离视频像素格式映射`
- `3d99c4c [功能修改] 抽离视频渲染材质逻辑`
- `e2f007e [功能修改] 抽离视频场景节点逻辑`
- `4d5795a [功能修改] 抽离视频显示区域计算`

## Task 6: Completed FFmpegDecoder Helper Extraction

**Files:**
- Created: `plugins/PlayPlugin/src/decode/DecodePerformance.h/.cpp`
- Created: `plugins/PlayPlugin/src/decode/VideoFrameProcessor.h/.cpp`
- Created: `plugins/PlayPlugin/src/decode/MediaOpener.h/.cpp`
- Created: `plugins/PlayPlugin/src/decode/StreamFrameDecoder.h/.cpp`
- Created: `plugins/PlayPlugin/src/decode/FFmpegLogBridge.h/.cpp`
- Modified: `plugins/PlayPlugin/src/FFmpegDecoder.h/.cpp`
- Modified: `plugins/PlayPlugin/CMakeLists.txt`
- Created: `tests/playplugin_decoder_decoupling_checks.py`
- Created: `tests/playplugin_video_frame_processor_checks.py`
- Created: `tests/playplugin_media_opener_checks.py`
- Created: `tests/playplugin_stream_frame_decoder_checks.py`
- Created: `tests/playplugin_ffmpeg_log_bridge_checks.py`

- [x] **Step 1: Extract decode performance counters and throttled logging**
- [x] **Step 2: Extract video frame preparation, hardware transfer, and pixel normalization**
- [x] **Step 3: Extract media input open, stream discovery, codec setup, and hardware backend selection**
- [x] **Step 4: Extract packet send, frame receive, and PTS normalization**
- [x] **Step 5: Extract FFmpeg global log callback bridge**
- [x] **Step 6: Update CMake and architecture checks**
- [x] **Step 7: Build and run CTest**

Commits:

- `6a10541 [功能修改] 抽离解码性能统计逻辑`
- `8607319 [功能修改] 抽离视频帧处理逻辑`
- `194f152 [功能修改] 抽离媒体打开逻辑`
- `ecfe173 [功能修改] 抽离流帧解码逻辑`
- `f866f4f [功能修改] 抽离 FFmpeg 日志桥接`

## Task 7: Completed Decode Loop Command State

**Goal:** Reduce the remaining complexity in `FFmpegDecoder::decodeLoop()` by isolating command and EOF waiting decisions.

**Candidate files:**
- Create: `plugins/PlayPlugin/src/decode/DecodeLoopControl.h`
- Create: `plugins/PlayPlugin/src/decode/DecodeLoopControl.cpp`
- Modify: `plugins/PlayPlugin/src/FFmpegDecoder.h`
- Modify: `plugins/PlayPlugin/src/FFmpegDecoder.cpp`
- Modify: `plugins/PlayPlugin/CMakeLists.txt`
- Create: `tests/playplugin_decode_loop_control_checks.py`

**Completed scope:**

- [x] **Step 1: Add architecture guard**

Require a helper boundary for pause/seek/EOF decision handling while keeping `FFmpegDecoder` as the signal emitter and queue owner.

- [x] **Step 2: Extract seek request consumption**

Move the repeated "lock, copy seek target/generation, clear request" pattern behind a small helper API. Keep `doSeek()` and `emit seekCompleted(...)` in `FFmpegDecoder` for this step.

- [x] **Step 3: Extract EOF wait decision**

Move the EOF wait loop decision into a helper that returns an explicit enum:

```cpp
enum class EofWaitDecision
{
    StopOrNewOpen,
    SeekRequested
};
```

- [x] **Step 4: Verify behavior**

Run:

```bash
python3 tests/playplugin_regression_checks.py
python3 tests/playplugin_decode_loop_control_checks.py
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

**Risk note:** This phase touches EOF and seek behavior, so it should be kept smaller than prior mechanical helper extractions.

Commits:

- `ef4af33 [功能修改] 抽离解码循环 seek 请求消费`
- `eebc0c6 [功能修改] 抽离 EOF 后等待决策`

## Task 8: Completed Playback State And Seek Coordination Boundaries

**Goal:** Reduce remaining facade responsibilities in `PlayerEngine` and `PlaybackPipeline` without changing QML API or playback behavior.

**Files:**
- Created: `plugins/PlayPlugin/src/PlaybackCompletionTracker.h/.cpp`
- Created: `plugins/PlayPlugin/src/PlaybackSeekCoordinator.h/.cpp`
- Modified: `plugins/PlayPlugin/src/PlayerEngine.h/.cpp`
- Modified: `plugins/PlayPlugin/src/PlaybackPipeline.h/.cpp`
- Modified: `plugins/PlayPlugin/CMakeLists.txt`
- Updated: `tests/playplugin_architecture_decoupling_checks.py`
- Updated: `tests/playplugin_regression_checks.py`

- [x] **Step 1: Extract media completion state**

Move decoder/audio/video completion flags and finish decision rules into `PlaybackCompletionTracker`. Keep public state changes and signal emission in `PlayerEngine`.

- [x] **Step 2: Extract seek coordination side effects**

Move seek start and seek completion ordering into `PlaybackSeekCoordinator`. Keep component ownership in `PlaybackPipeline`.

- [x] **Step 3: Verify behavior**

Run:

```bash
python3 tests/playplugin_architecture_decoupling_checks.py
python3 tests/playplugin_regression_checks.py
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

**Risk note:** These helpers intentionally remain small non-QObject helpers. Adding a larger state machine or pimpl should wait until there is a concrete behavior change that needs it.
