# PlayPlugin Architecture Decoupling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce `PlaybackPipeline` so `PlayerEngine` becomes a QML-facing facade instead of directly owning decoder, renderer, queues, and clock.

**Architecture:** Keep public QML API stable. Add an internal QObject `PlaybackPipeline` that owns low-level playback components, forwards their signals, and provides component lifecycle commands. Keep completion state in `PlayerEngine` for this phase to minimize behavior risk.

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

## Task 1: Add Architecture Guard

**Files:**
- Create: `tests/playplugin_architecture_decoupling_checks.py`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing architecture test**

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

- [ ] **Step 2: Run the test and verify RED**

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

- [ ] **Step 1: Add PlaybackPipeline header and implementation**

Create `PlaybackPipeline` as a QObject that owns queues, clock, decoder, audio renderer, and video renderer. It must expose the commands and signals described in the design document.

- [ ] **Step 2: Add files to CMake**

Add `src/PlaybackPipeline.h src/PlaybackPipeline.cpp` to the `qt_add_qml_module(PlayPlugin SOURCES ...)` list near `PlayerEngine`.

- [ ] **Step 3: Run the architecture test**

Run:

```bash
python3 tests/playplugin_architecture_decoupling_checks.py
```

Expected: still FAIL because `PlayerEngine` has not delegated yet.

## Task 3: Delegate PlayerEngine To PlaybackPipeline

**Files:**
- Modify: `plugins/PlayPlugin/src/PlayerEngine.h`
- Modify: `plugins/PlayPlugin/src/PlayerEngine.cpp`

- [ ] **Step 1: Replace direct members**

In `PlayerEngine.h`, forward declare `PlaybackPipeline`, remove direct includes for low-level playback classes, and replace direct low-level members with:

```cpp
std::unique_ptr<PlaybackPipeline> m_pipeline;
```

- [ ] **Step 2: Wire pipeline signals**

In the constructor, create `m_pipeline` and connect pipeline signals to existing `PlayerEngine` slots.

- [ ] **Step 3: Delegate commands**

Replace direct decoder/renderer/queue/surface operations with equivalent `m_pipeline` calls.

- [ ] **Step 4: Run the architecture test**

Run:

```bash
python3 tests/playplugin_architecture_decoupling_checks.py
```

Expected: PASS with no output.

## Task 4: Verify Existing Regression Coverage

**Files:**
- No source edits expected.

- [ ] **Step 1: Run static PlayPlugin regression checks**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: PASS with no output.

- [ ] **Step 2: Run CTest if a configured build exists**

Run:

```bash
ctest --test-dir build --output-on-failure
```

Expected: all configured tests pass. If `build` is not configured locally, configure/build first using the repository build instructions.

- [ ] **Step 3: Build**

Run:

```bash
cmake --build build --parallel
```

Expected: `PlayPlugin` and `PluginBasedApp` build successfully.
