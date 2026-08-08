# Build And Packaging Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Release tests meaningful and produce a repeatable, self-contained macOS package that loads themes and both business plugins.

**Architecture:** Keep the current package entry point and extract focused deployment helpers inside `tools/deploy.py`. Add behavior-level Python regression coverage and a test-only CMake assertion policy, then clean the platform and compiler warnings without touching media runtime contracts.

**Tech Stack:** Python 3.9, CMake 3.21+, C++17, Qt 6, macdeployqt, hdiutil.

## Global Constraints

- Preserve the current plugin API, ABI, manifest schema, and runtime lookup paths.
- Do not change playback, rendering, ownership, or thread-affinity behavior.
- Do not enable assertions for production Release targets.
- Preserve the current `package.sh` CLI and platform layouts.
- Preserve all pre-existing user changes in the dirty worktree.

---

### Task 1: Packaging resource and Qt path regressions

**Files:**
- Create: `tests/deploy_packaging_checks.py`
- Modify: `CMakeLists.txt`
- Modify: `tools/deploy.py`

- [ ] Write temporary-tree tests proving plugin manifests, metadata, and themes enter platform runtime layouts.
- [ ] Write temporary-tree tests proving `plugins/platforms/...` retains `platforms/` and Qt query paths override guessed prefixes.
- [ ] Write a mocked-command test proving an existing Frameworks rpath is not added twice.
- [ ] Run `python3 tests/deploy_packaging_checks.py` and confirm it fails for the missing helpers/current behavior.
- [ ] Implement the minimal shared resource-copy, Qt path-query, and idempotent-rpath helpers.
- [ ] Register the Python test with CTest and rerun it to green.

### Task 2: Release assertion contract

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `sdk/media_core/CMakeLists.txt`
- Modify: `sdk/media_playback_runtime/CMakeLists.txt`
- Modify: `sdk/media_playback_session/CMakeLists.txt`
- Modify: `sdk/media_audio_ffmpeg/CMakeLists.txt`
- Modify: `sdk/media_platform_audio_macos/CMakeLists.txt`
- Modify: `tools/video_benchmark/CMakeLists.txt`

- [ ] Use the current seven Release crashes as the red functional evidence.
- [ ] Add a test-only interface target that undefines `NDEBUG` portably.
- [ ] Link only executable test targets to the interface target in each test-producing directory.
- [ ] Rebuild the seven failed targets and run `ctest --test-dir build-release --rerun-failed --output-on-failure`.
- [ ] Run all Release tests and confirm assertions execute.

### Task 3: Platform and warning cleanup

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `app/CMakeLists.txt`
- Modify: `app/CrashHandler.cpp`
- Modify: `sdk/media_platform_audio_macos/src/CoreAudioOutputEngine.cpp`
- Modify: `plugins/PlayPlugin/CMakeLists.txt`

- [ ] Make QTP0001/QTP0004 choices explicit while preserving the current resource layout.
- [ ] Replace the Linux-on-UNIX definition with a POSIX definition for Linux/macOS and update the crash-handler message.
- [ ] Explicitly discard the CoreAudio ring-buffer diagnostic result.
- [ ] Remove only direct SDK links already provided transitively by `media_sdk_playback_session`.
- [ ] Build Release and review output for the original warnings.

### Task 4: End-to-end verification

**Files:**
- Modify only if a verification failure demonstrates a remaining root cause.

- [ ] Run all Debug and Release CTest suites.
- [ ] Run `./package.sh --skip-build build-release` twice and confirm both invocations exit zero without duplicate-rpath errors.
- [ ] Run `python3 tools/verify.py --stage-dir build-release/app/PluginBasedApp.app`.
- [ ] Run `codesign --verify --deep --strict` and `hdiutil verify`.
- [ ] Start the Cocoa app briefly and confirm the theme path is bundle-local and two plugins load.
- [ ] Report the artifact path, checksum, exact verification, and any residual risk.
