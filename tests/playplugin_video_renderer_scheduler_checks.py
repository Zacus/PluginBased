#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    renderer_h = read("plugins/PlayPlugin/src/video/VideoRenderer.h")
    renderer_cpp = read("plugins/PlayPlugin/src/video/VideoRenderer.cpp")
    frame_queue_h = read("plugins/PlayPlugin/src/common/FrameQueue.h")
    flow_doc = read("plugins/PlayPlugin/PlayPluginExecutionFlow.md")
    root_cmake = read("CMakeLists.txt")

    require("m_timer.setInterval(8)" not in renderer_cpp,
            "VideoRenderer must not use a fixed 8ms polling interval")
    require("queueEmptyPolls" not in renderer_h + renderer_cpp,
            "VideoRenderer performance stats should not describe empty wakeups as polling")
    require("queueEmptyWakeups" in renderer_h + renderer_cpp,
            "VideoRenderer should count empty scheduler wakeups explicitly")
    require("empty_wake=" in renderer_cpp,
            "VideoRenderer performance log should label empty scheduler wakeups")
    require("m_timer.setSingleShot(true)" in renderer_cpp,
            "VideoRenderer timer should be single-shot")
    require("m_timer.setTimerType(Qt::PreciseTimer)" in renderer_cpp,
            "VideoRenderer should use Qt::PreciseTimer for frame-time scheduling")
    require('#include "video/VideoFrameScheduler.h"' in renderer_h,
            "VideoRenderer should depend on the frame scheduler calculation unit")
    require("scheduleNextCheck" in renderer_h and "scheduleNextCheck" in renderer_cpp,
            "VideoRenderer should centralize dynamic timer scheduling")
    require("cancelScheduledCheck" in renderer_h and "cancelScheduledCheck" in renderer_cpp,
            "VideoRenderer should centralize dynamic timer cancellation")
    require(renderer_cpp.count("cancelScheduledCheck();") >= 5,
            "stop, pause, flush, reset, and seek should cancel pending frame checks")
    require("waitIntervalMs(decision.waitUs)" in renderer_cpp,
            "Wait frames should schedule from the calculated frame wait time")
    require("setWakeCallback" in frame_queue_h,
            "FrameQueue should expose a non-blocking wake callback for GUI consumers")
    require("notifyWakeCallback" in frame_queue_h,
            "FrameQueue should notify callbacks after successful push operations")
    require("setWakeCallback" in renderer_cpp,
            "VideoRenderer should subscribe to video queue wakeups")
    require("QPointer<VideoRenderer>" in renderer_cpp,
            "VideoRenderer queue wake callback should guard queued delivery with QPointer")
    require("notifyFrameAvailable" in renderer_h and "notifyFrameAvailable" in renderer_cpp,
            "VideoRenderer should handle queue wakeups through a dedicated method")
    require("processNextFrame" in renderer_h and "processNextFrame" in renderer_cpp,
            "VideoRenderer should keep frame processing in a reusable processNextFrame method")
    require("void VideoRenderer::onTimer()\n{\n    processNextFrame();\n}" in renderer_cpp,
            "VideoRenderer timer slot should only delegate to processNextFrame")
    require("m_queue->setWakeCallback({})" in renderer_cpp,
            "VideoRenderer should clear the queue wake callback during destruction")
    require("if (m_hasHeld)" in renderer_cpp,
            "VideoRenderer should not let queue wakeups preempt a held frame")
    require("8 ms `QTimer`" not in flow_doc and "120 fps 上限" not in flow_doc,
            "PlayPlugin execution flow should not document the old fixed 8ms timer")
    require("新帧唤醒" in flow_doc and "PTS" in flow_doc and "single-shot" in flow_doc,
            "PlayPlugin execution flow should document frame-time scheduling")
    require("add_test(NAME playplugin_video_renderer_scheduler_checks" in root_cmake,
            "CTest should run the video renderer scheduler check")


if __name__ == "__main__":
    main()
