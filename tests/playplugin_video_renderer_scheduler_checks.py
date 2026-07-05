#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    runtime_scheduler = read("sdk/media_playback_runtime/src/AvSyncScheduler.h")
    runtime_player = read("sdk/media_playback_runtime/src/RuntimePlayer.cpp")
    cmake = read("plugins/PlayPlugin/CMakeLists.txt")

    require(not (ROOT / "plugins/PlayPlugin/src/video/VideoRenderer.h").exists(),
            "PlayPlugin VideoRenderer should be removed after SDK runtime owns scheduling")
    require(not (ROOT / "plugins/PlayPlugin/src/video/VideoFrameScheduler.h").exists(),
            "PlayPlugin VideoFrameScheduler should be removed after SDK runtime owns scheduling")
    require("class AvSyncScheduler" in runtime_scheduler,
            "SDK runtime should own A/V frame scheduling")
    require("waitForFrameTime" in runtime_player and "waitForPresentCapacity" in runtime_player,
            "SDK runtime should own frame-time waiting and presenter backpressure")
    require("VideoRenderer" not in cmake and "VideoFrameScheduler" not in cmake,
            "PlayPlugin CMake should not compile old video scheduling components")


if __name__ == "__main__":
    main()
