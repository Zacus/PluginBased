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
    expected_layout = (
        "playback/PlaybackContext.h",
        "playback/PlaybackContext.cpp",
        "playback/PlaybackCompletionTracker.h",
        "playback/PlaybackCompletionTracker.cpp",
        "playback/PlaybackPipeline.h",
        "playback/PlaybackPipeline.cpp",
        "playback/QtPlaybackAdapter.h",
        "playback/QtPlaybackAdapter.cpp",
        "playback/PlayerEngine.h",
        "playback/PlayerEngine.cpp",
        "model/PlaylistModel.h",
        "model/PlaylistModel.cpp",
        "model/MediaInfo.h",
        "audio/AudioRenderer.h",
        "audio/AudioRenderer.cpp",
        "video/VideoRenderer.h",
        "video/VideoRenderer.cpp",
        "video/FFmpegSurface.h",
        "video/FFmpegSurface.cpp",
        "video/render/VideoMaterial.h",
        "video/render/VideoMaterial.cpp",
        "video/native/NativeVideoFrame.h",
        "sync/ClockSync.h",
        "common/FFmpegUtils.h",
        "common/FrameQueue.h",
    )
    for relative_path in expected_layout:
        require((ROOT / "plugins/PlayPlugin/src" / relative_path).exists(),
                f"PlayPlugin source should live under src/{relative_path}")

    old_root_files = (
        "PlaybackContext.h",
        "PlaybackCompletionTracker.h",
        "PlaybackPipeline.h",
        "QtPlaybackAdapter.h",
        "PlayerEngine.h",
        "PlaylistModel.h",
        "MediaInfo.h",
        "AudioRenderer.h",
        "VideoRenderer.h",
        "FFmpegSurface.h",
        "ClockSync.h",
        "FFmpegUtils.h",
        "FrameQueue.h",
    )
    for filename in old_root_files:
        require(not (ROOT / "plugins/PlayPlugin/src" / filename).exists(),
                f"PlayPlugin source root should not keep categorized file: {filename}")

    pipeline_h_path = ROOT / "plugins/PlayPlugin/src/playback/PlaybackPipeline.h"
    pipeline_cpp_path = ROOT / "plugins/PlayPlugin/src/playback/PlaybackPipeline.cpp"
    completion_h_path = ROOT / "plugins/PlayPlugin/src/playback/PlaybackCompletionTracker.h"
    completion_cpp_path = ROOT / "plugins/PlayPlugin/src/playback/PlaybackCompletionTracker.cpp"
    adapter_h_path = ROOT / "plugins/PlayPlugin/src/playback/QtPlaybackAdapter.h"
    adapter_cpp_path = ROOT / "plugins/PlayPlugin/src/playback/QtPlaybackAdapter.cpp"
    require(pipeline_h_path.exists(), "PlaybackPipeline.h should exist")
    require(pipeline_cpp_path.exists(), "PlaybackPipeline.cpp should exist")
    require(completion_h_path.exists(), "PlaybackCompletionTracker.h should exist")
    require(completion_cpp_path.exists(), "PlaybackCompletionTracker.cpp should exist")
    require(adapter_h_path.exists(), "QtPlaybackAdapter.h should exist")
    require(adapter_cpp_path.exists(), "QtPlaybackAdapter.cpp should exist")

    engine_h = read("plugins/PlayPlugin/src/playback/PlayerEngine.h")
    engine_cpp = read("plugins/PlayPlugin/src/playback/PlayerEngine.cpp")
    pipeline_h = read("plugins/PlayPlugin/src/playback/PlaybackPipeline.h")
    pipeline_cpp = read("plugins/PlayPlugin/src/playback/PlaybackPipeline.cpp")
    completion_h = read("plugins/PlayPlugin/src/playback/PlaybackCompletionTracker.h")
    completion_cpp = read("plugins/PlayPlugin/src/playback/PlaybackCompletionTracker.cpp")
    adapter_h = read("plugins/PlayPlugin/src/playback/QtPlaybackAdapter.h")
    adapter_cpp = read("plugins/PlayPlugin/src/playback/QtPlaybackAdapter.cpp")
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

    require("QtPlaybackAdapter" in pipeline_h and "AudioRenderer" in pipeline_h and "VideoRenderer" in pipeline_h,
            "PlaybackPipeline should own the SDK Qt adapter and renderers")
    require("std::unique_ptr<FFmpegDecoder>" not in pipeline_h,
            "PlaybackPipeline should no longer directly own FFmpegDecoder after SDK adapter integration")
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
    require("跟踪解码器、音频和视频三路完成状态" in completion_h,
            "PlaybackCompletionTracker.h should explain its file purpose")
    require("实现 PlayerEngine 使用的媒体完成状态转换" in completion_cpp,
            "PlaybackCompletionTracker.cpp should explain its file purpose")
    require("bool shouldFinish() const" in completion_h and "bool finish()" in completion_h,
            "PlaybackCompletionTracker should expose finish decision and transition")
    require("m_adapter->seekTo(positionMs, generation)" in pipeline_cpp,
            "PlaybackPipeline::seek should submit seek through QtPlaybackAdapter")
    require("m_videoRenderer->beginSeek(generation)" in pipeline_cpp and
            "m_audioRenderer->flush()" in pipeline_cpp and
            "m_videoRenderer->flush()" in pipeline_cpp,
            "PlaybackPipeline should coordinate renderer and queue seek side effects")
    require("m_decoder->seekTo(positionMs, generation)" not in pipeline_cpp,
            "PlaybackPipeline should not call FFmpegDecoder seek after SDK adapter integration")
    require("public media_sdk::IEventSink" in adapter_h and
            "std::unique_ptr<media_sdk::Player>" in adapter_h and
            "QMetaObject::invokeMethod" in adapter_cpp,
            "QtPlaybackAdapter should own the SDK player and marshal events to Qt")
    require(not (ROOT / "plugins/PlayPlugin/src/decode").exists(),
            "PlayPlugin should not keep the migrated decode core sources")
    require("PlaybackPipeline.h" in cmake and "PlaybackPipeline.cpp" in cmake and
            "PlaybackCompletionTracker.h" in cmake and "PlaybackCompletionTracker.cpp" in cmake and
            "QtPlaybackAdapter.h" in cmake and "QtPlaybackAdapter.cpp" in cmake,
            "PlayPlugin CMake should compile playback boundary helpers")
    require("playplugin_architecture_decoupling_checks" in root_cmake,
            "CTest should run the architecture decoupling check")


if __name__ == "__main__":
    main()
