#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    pipeline_h = read("plugins/PlayPlugin/src/playback/PlaybackPipeline.h")
    pipeline_cpp = read("plugins/PlayPlugin/src/playback/PlaybackPipeline.cpp")
    engine_h = read("plugins/PlayPlugin/src/playback/PlayerEngine.h")
    engine_cpp = read("plugins/PlayPlugin/src/playback/PlayerEngine.cpp")
    cmake = read("plugins/PlayPlugin/CMakeLists.txt")

    require("SdkPlaybackAdapter" in pipeline_h + pipeline_cpp,
            "PlaybackPipeline should route playback through SdkPlaybackAdapter")
    require("QtRhiVideoPresenter" in pipeline_h + pipeline_cpp,
            "PlaybackPipeline should keep Qt RHI presentation in PlayPlugin")
    require("setPlaybackRuntimeMode" not in engine_h + engine_cpp,
            "PlayerEngine should not expose runtime switching after SDK-only migration")
    require("playbackRuntimeMode" not in engine_h + engine_cpp,
            "QML API should not expose the removed LegacyQt mode")

    for token in (
        "QtPlaybackAdapter",
        "AudioRenderer",
        "VideoRenderer",
        "ClockSync",
        "PlaybackDataBridge",
        "VideoFrameScheduler",
        "FrameQueue",
        "LegacyQt",
    ):
        require(token not in pipeline_h + pipeline_cpp,
                f"PlaybackPipeline should not reference removed legacy component: {token}")
        require(token not in cmake,
                f"PlayPlugin CMake should not compile removed legacy component: {token}")

    require("media_sdk::playback_session" in cmake,
            "PlayPlugin should link SDK playback session")


if __name__ == "__main__":
    main()
