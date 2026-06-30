# PlayPlugin Seek Risk Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复 review 发现的 seek 合并协议、EOF drain 暂停语义和队列取消竞态风险。

**Architecture:** SDK core 允许连续 seek 合并为一次 `SeekCompletedEvent`，PlayPlugin adapter 必须按完成位置映射到最新匹配的 UI/runtime 请求。EOF drain seek 的完成状态重置留在 `PlaybackCompletionTracker`，是否恢复播放由 `PlayerEngine` 传入 seek 前状态。FrameQueue 提供取消 epoch，DataBridge push/finish 使用 epoch 闭环防止 cancel 后旧帧进入队列。

**Tech Stack:** C++17, Qt 6, CMake/CTest, FFmpeg-backed PlayPlugin queues.

---

### Task 1: Pending Seek Completion Mapping

**Files:**
- Create: `plugins/PlayPlugin/src/playback/PendingSeekRequests.h`
- Create: `tests/tst_playplugin_pending_seek_requests.cpp`
- Modify: `plugins/PlayPlugin/src/playback/QtPlaybackAdapter.h`
- Modify: `plugins/PlayPlugin/src/playback/QtPlaybackAdapter.cpp`
- Modify: `plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.h`
- Modify: `plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp`
- Modify: `CMakeLists.txt`

- [ ] Write failing test for exact, coalesced, queued, and duplicate-position seek completion mapping.
- [ ] Run `cmake --build build --target PlayPluginPendingSeekRequestsTest` and verify the target/header is missing.
- [ ] Add `PendingSeekRequests<T>` and use it in both adapters.
- [ ] Re-run `PlayPluginPendingSeekRequestsTest`.

### Task 2: EOF Drain Pause Semantics

**Files:**
- Modify: `plugins/PlayPlugin/src/playback/PlaybackCompletionTracker.h`
- Modify: `plugins/PlayPlugin/src/playback/PlaybackCompletionTracker.cpp`
- Modify: `plugins/PlayPlugin/src/playback/PlayerEngine.cpp`
- Modify: `tests/tst_playplugin_playback_completion.cpp`

- [ ] Write failing tests showing decoder-drain seek resumes only when playback was playing, while fully finished media still resumes.
- [ ] Run `cmake --build build --target PlayPluginPlaybackCompletionTest` and verify the signature/behavior fails.
- [ ] Add `resumeAfterFinishedSeek(bool playbackWasPlaying)` and pass `m_state == Playing` from `PlayerEngine`.
- [ ] Re-run `PlayPluginPlaybackCompletionTest`.

### Task 3: FrameQueue Cancel Epoch Closure

**Files:**
- Modify: `plugins/PlayPlugin/src/common/FrameQueue.h`
- Modify: `plugins/PlayPlugin/src/playback/PlaybackDataBridge.cpp`
- Modify: `tests/tst_playplugin_frame_queue.cpp`

- [ ] Write failing tests for `pushIfCancelSerial()` and `finishIfCancelSerial()` rejecting work after `cancelPendingPushes()`.
- [ ] Run `cmake --build build --target PlayPluginFrameQueueTest` and verify the methods are missing.
- [ ] Add cancel epoch APIs and route DataBridge push/finish through them.
- [ ] Re-run `PlayPluginFrameQueueTest`.

### Task 4: Verification

**Files:**
- No source changes unless verification exposes a regression.

- [ ] Run focused tests for the three touched areas.
- [ ] Run `cmake --build build --parallel`.
- [ ] Run `ctest --test-dir build --output-on-failure`.
- [ ] Run `git diff --check`.
