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
    root_cmake = read("CMakeLists.txt")

    require("m_timer.setInterval(8)" not in renderer_cpp,
            "VideoRenderer must not use a fixed 8ms polling interval")
    require("m_timer.setSingleShot(true)" in renderer_cpp,
            "VideoRenderer timer should be single-shot")
    require("m_timer.setTimerType(Qt::PreciseTimer)" in renderer_cpp,
            "VideoRenderer should use Qt::PreciseTimer for frame-time scheduling")
    require('#include "video/VideoFrameScheduler.h"' in renderer_h,
            "VideoRenderer should depend on the frame scheduler calculation unit")
    require("scheduleNextCheck" in renderer_h and "scheduleNextCheck" in renderer_cpp,
            "VideoRenderer should centralize dynamic timer scheduling")
    require("waitIntervalMs(decision.waitUs)" in renderer_cpp,
            "Wait frames should schedule from the calculated frame wait time")
    require("add_test(NAME playplugin_video_renderer_scheduler_checks" in root_cmake,
            "CTest should run the video renderer scheduler check")


if __name__ == "__main__":
    main()
