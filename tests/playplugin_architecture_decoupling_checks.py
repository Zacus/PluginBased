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
    completion_h_path = ROOT / "plugins/PlayPlugin/src/PlaybackCompletionTracker.h"
    completion_cpp_path = ROOT / "plugins/PlayPlugin/src/PlaybackCompletionTracker.cpp"
    seek_h_path = ROOT / "plugins/PlayPlugin/src/PlaybackSeekCoordinator.h"
    seek_cpp_path = ROOT / "plugins/PlayPlugin/src/PlaybackSeekCoordinator.cpp"
    require(pipeline_h_path.exists(), "PlaybackPipeline.h should exist")
    require(pipeline_cpp_path.exists(), "PlaybackPipeline.cpp should exist")
    require(completion_h_path.exists(), "PlaybackCompletionTracker.h should exist")
    require(completion_cpp_path.exists(), "PlaybackCompletionTracker.cpp should exist")
    require(seek_h_path.exists(), "PlaybackSeekCoordinator.h should exist")
    require(seek_cpp_path.exists(), "PlaybackSeekCoordinator.cpp should exist")

    engine_h = read("plugins/PlayPlugin/src/PlayerEngine.h")
    engine_cpp = read("plugins/PlayPlugin/src/PlayerEngine.cpp")
    pipeline_h = read("plugins/PlayPlugin/src/PlaybackPipeline.h")
    pipeline_cpp = read("plugins/PlayPlugin/src/PlaybackPipeline.cpp")
    completion_h = read("plugins/PlayPlugin/src/PlaybackCompletionTracker.h")
    completion_cpp = read("plugins/PlayPlugin/src/PlaybackCompletionTracker.cpp")
    seek_h = read("plugins/PlayPlugin/src/PlaybackSeekCoordinator.h")
    seek_cpp = read("plugins/PlayPlugin/src/PlaybackSeekCoordinator.cpp")
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
    require("PlaybackCompletionTracker m_completion" in engine_h,
            "PlayerEngine should delegate media completion flags to PlaybackCompletionTracker")
    for forbidden in (
        "m_decoderFinished",
        "m_audioFinished",
        "m_videoFinished",
        "m_mediaFinished",
    ):
        require(forbidden not in engine_h,
                f"PlayerEngine should not own completion flag directly: {forbidden}")
    require("Tracks decoder/audio/video completion state for PlayerEngine" in completion_h,
            "PlaybackCompletionTracker.h should explain its file purpose")
    require("Implements media completion state transitions for PlayerEngine" in completion_cpp,
            "PlaybackCompletionTracker.cpp should explain its file purpose")
    require("bool shouldFinish() const" in completion_h and "bool finish()" in completion_h,
            "PlaybackCompletionTracker should expose finish decision and transition")
    require("PlaybackSeekCoordinator m_seekCoordinator" in pipeline_h,
            "PlaybackPipeline should delegate seek coordination to PlaybackSeekCoordinator")
    require("m_seekCoordinator.seek(positionMs, generation)" in pipeline_cpp,
            "PlaybackPipeline::seek should delegate renderer/decoder/clock coordination")
    require("Coordinates playback seek side effects owned by PlaybackPipeline" in seek_h,
            "PlaybackSeekCoordinator.h should explain its file purpose")
    require("Implements ordered seek coordination across decoder, renderers, queues, and clock" in seek_cpp,
            "PlaybackSeekCoordinator.cpp should explain its file purpose")
    require("void seek(qint64 positionMs, int generation)" in seek_h and
            "void complete(int generation, int serial)" in seek_h,
            "PlaybackSeekCoordinator should expose seek start and completion coordination")
    for forbidden in (
        "m_videoRenderer->beginSeek(generation)",
        "m_audioRenderer->setAcceptedSerial(generation)",
        "m_decoder->seekTo(positionMs, generation)",
        "m_audioRenderer->flush()",
        "m_videoRenderer->flush()",
    ):
        require(forbidden not in pipeline_cpp,
                f"PlaybackPipeline should not directly perform seek side effect: {forbidden}")
    require("PlaybackPipeline.h" in cmake and "PlaybackPipeline.cpp" in cmake and
            "PlaybackCompletionTracker.h" in cmake and "PlaybackCompletionTracker.cpp" in cmake and
            "PlaybackSeekCoordinator.h" in cmake and "PlaybackSeekCoordinator.cpp" in cmake,
            "PlayPlugin CMake should compile playback boundary helpers")
    require("playplugin_architecture_decoupling_checks" in root_cmake,
            "CTest should run the architecture decoupling check")


if __name__ == "__main__":
    main()
