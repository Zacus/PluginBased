#!/usr/bin/env python3

"""Architecture guard for the PlayPlugin PlayerEngine/PlaybackPipeline boundary."""

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
