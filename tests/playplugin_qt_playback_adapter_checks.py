#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    adapter_h_path = ROOT / "plugins/PlayPlugin/src/playback/QtPlaybackAdapter.h"
    adapter_cpp_path = ROOT / "plugins/PlayPlugin/src/playback/QtPlaybackAdapter.cpp"
    require(adapter_h_path.exists(), "QtPlaybackAdapter.h should exist")
    require(adapter_cpp_path.exists(), "QtPlaybackAdapter.cpp should exist")

    adapter_h = read("plugins/PlayPlugin/src/playback/QtPlaybackAdapter.h")
    adapter_cpp = read("plugins/PlayPlugin/src/playback/QtPlaybackAdapter.cpp")
    pipeline_h = read("plugins/PlayPlugin/src/playback/PlaybackPipeline.h")
    pipeline_cpp = read("plugins/PlayPlugin/src/playback/PlaybackPipeline.cpp")
    cmake = read("plugins/PlayPlugin/CMakeLists.txt")

    require("media_sdk::IEventSink" in adapter_h,
            "QtPlaybackAdapter should implement the SDK IEventSink boundary")
    require("Q_OBJECT" in adapter_h,
            "QtPlaybackAdapter should be a QObject so Qt signals are GUI-thread owned")
    require("media_sdk::Player" in adapter_h or "media_sdk::Player" in adapter_cpp,
            "QtPlaybackAdapter should own media_sdk::Player")
    require("QMetaObject::invokeMethod" in adapter_cpp and "Qt::QueuedConnection" in adapter_cpp,
            "QtPlaybackAdapter should marshal SDK events to the Qt object thread")
    require("VideoFrameQueue" in adapter_h and "AudioFrameQueue" in adapter_h,
            "QtPlaybackAdapter should bridge SDK frames into existing PlayPlugin queues")
    require("AudioFrameEvent" in adapter_cpp and "VideoFrameEvent" in adapter_cpp,
            "QtPlaybackAdapter should handle SDK audio and video frame events")
    require("mediaInfoReady" in adapter_h and "endOfFile" in adapter_h and "seekCompleted" in adapter_h,
            "QtPlaybackAdapter should expose decoder-compatible Qt signals")

    require("src/playback/QtPlaybackAdapter.h" in cmake and
            "src/playback/QtPlaybackAdapter.cpp" in cmake,
            "PlayPlugin CMake should compile QtPlaybackAdapter")
    require("media_sdk_core" in cmake or "media_sdk::core" in cmake,
            "PlayPlugin should link the media SDK core target")

    require("QtPlaybackAdapter" in pipeline_h + pipeline_cpp,
            "PlaybackPipeline should use QtPlaybackAdapter")
    require("std::unique_ptr<FFmpegDecoder>" not in pipeline_h,
            "PlaybackPipeline should no longer directly own FFmpegDecoder")
    require("PlaybackSeekCoordinator" not in pipeline_h,
            "PlaybackPipeline should own seek coordination after switching to the SDK adapter")


if __name__ == "__main__":
    main()
