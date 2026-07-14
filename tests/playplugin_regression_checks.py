#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    engine_h = read("plugins/PlayPlugin/src/playback/PlayerEngine.h")
    engine_cpp = read("plugins/PlayPlugin/src/playback/PlayerEngine.cpp")
    pipeline_h = read("plugins/PlayPlugin/src/playback/PlaybackPipeline.h")
    pipeline_cpp = read("plugins/PlayPlugin/src/playback/PlaybackPipeline.cpp")
    sdk_adapter_h = read("plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.h")
    sdk_adapter_cpp = read("plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp")
    presenter_cpp = read("plugins/PlayPlugin/src/playback/QtRhiVideoPresenter.cpp")
    ffmpeg_surface_cpp = read("plugins/PlayPlugin/src/video/FFmpegSurface.cpp")
    control_bar_qml = read("plugins/PlayPlugin/qml/ControlBar.qml")
    cmake = read("plugins/PlayPlugin/CMakeLists.txt")

    require("PlaybackCompletionTracker" in engine_h,
            "PlayerEngine should keep completion tracking for SDK EOF/audio/video drain")
    require("m_completion.setStreams" in engine_cpp,
            "PlayerEngine should derive completion streams from SDK media info")
    require("m_pipeline->seek(positionMs, seekGeneration, resumeAfterSeek)" in engine_cpp,
            "PlayerEngine should keep generation-based seek completion and pass resume intent")
    require("m_position = posMs" in engine_cpp[engine_cpp.find("void PlayerEngine::onDecoderPosition"):],
            "SDK decoder position should drive UI progress")

    require("std::unique_ptr<SdkPlaybackAdapter>" in pipeline_h,
            "PlaybackPipeline should use SDK playback adapter")
    require("FfmpegAudioTempoProcessor" in pipeline_h + pipeline_cpp,
            "PlaybackPipeline should own the FFmpeg tempo processor")
    require("CoreAudioAudioOutput" in pipeline_cpp,
            "PlaybackPipeline should inject SDK platform audio output")
    require("QtRhiVideoPresenter" in pipeline_cpp,
            "PlaybackPipeline should inject Qt RHI video presenter")
    require("destroySdkRuntimeChain" in pipeline_cpp,
            "PlaybackPipeline should explicitly tear down SDK runtime chain")

    require("std::unique_ptr<ISdkPlaybackSession>" in sdk_adapter_h
            and "RealSdkPlaybackSession" in sdk_adapter_cpp,
            "SdkPlaybackAdapter should own an injectable facade over SDK PlaybackSession")
    require("acceptsEventSerial" in sdk_adapter_cpp,
            "SdkPlaybackAdapter should reject stale queued Qt callbacks")
    require("PlayPerf: sdk" in sdk_adapter_cpp,
            "SDK diagnostics should remain observable in PlayPlugin logs")
    require("PlaybackRateChangedEvent" in sdk_adapter_cpp
            and "m_rateChangePending" in sdk_adapter_h,
            "SdkPlaybackAdapter should confirm or roll back asynchronous rate changes")

    require("Q_PROPERTY(double     playbackRate" in engine_h
            and "playbackRateChangePending" in engine_h
            and "onPlaybackRateChanged" in engine_cpp,
            "PlayerEngine should expose confirmed playback rate and pending state")

    for token in (
        "QtPlaybackAdapter",
        "PlaybackDataBridge",
        "AudioRenderer",
        "VideoRenderer",
        "VideoFrameScheduler",
        "ClockSync",
        "FrameQueue",
        "LegacyQt",
    ):
        require(token not in pipeline_h + pipeline_cpp + cmake,
                f"PlayPlugin runtime path should not reference removed component: {token}")

    require("supportsNativeVideoToolboxRendering" in ffmpeg_surface_cpp + read("plugins/PlayPlugin/src/video/FFmpegSurface.h"),
            "FFmpegSurface should still expose native rendering capability")
    require("diagnosticsSnapshot()" in ffmpeg_surface_cpp,
            "FFmpegSurface should keep render diagnostics at the Qt rendering boundary")
    require("nativeRenderingFailed" in pipeline_cpp,
            "PlaybackPipeline should react to native rendering failures from the Qt surface")

    require("onMoved: root.seekRequested" not in control_bar_qml,
            "Seek bar should not flood SDK seek while dragging")
    require("onPressedChanged:" in control_bar_qml
            and "root.seekRequested(value * root.duration)" in control_bar_qml,
            "Seek bar should submit one seek when the drag is released")
    require("ComboBox" in control_bar_qml
            and "playbackRateRequested" in control_bar_qml
            and all(rate in control_bar_qml
                    for rate in ("0.5x", "0.75x", "1.0x", "1.25x", "1.5x", "2.0x")),
            "ControlBar should expose all supported playback-rate presets")
    require("playbackRate:  engine.playbackRate" in read("plugins/PlayPlugin/qml/PlayerView.qml")
            and "engine.playbackRate = rate" in read("plugins/PlayPlugin/qml/PlayerView.qml"),
            "PlayerView should bind the confirmed rate and submit rate selections")

    require("media_sdk::playback_session" in cmake,
            "PlayPlugin should link playback session")
    require("media_sdk::audio_ffmpeg" in cmake,
            "PlayPlugin should link the FFmpeg tempo processor")
    require("media_sdk::platform_audio_macos" in cmake,
            "PlayPlugin should link macOS SDK audio platform implementation on Apple")


if __name__ == "__main__":
    main()
