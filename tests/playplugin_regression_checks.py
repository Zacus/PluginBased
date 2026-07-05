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
    require("m_pipeline->seek(positionMs, seekGeneration)" in engine_cpp,
            "PlayerEngine should keep generation-based seek completion")
    require("m_position = posMs" in engine_cpp[engine_cpp.find("void PlayerEngine::onDecoderPosition"):],
            "SDK decoder position should drive UI progress")

    require("std::unique_ptr<SdkPlaybackAdapter>" in pipeline_h,
            "PlaybackPipeline should use SDK playback adapter")
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
    require("diagnosticsSnapshot()" in presenter_cpp,
            "QtRhiVideoPresenter should still report presentation diagnostics")
    require("nativeTextureFailed" in presenter_cpp and "cpuMemcpy" in presenter_cpp,
            "QtRhiVideoPresenter should preserve native and CPU fallback diagnostics")

    require("onMoved: root.seekRequested" not in control_bar_qml,
            "Seek bar should not flood SDK seek while dragging")
    require("onPressedChanged:" in control_bar_qml
            and "root.seekRequested(value * root.duration)" in control_bar_qml,
            "Seek bar should submit one seek when the drag is released")

    require("media_sdk::playback_session" in cmake,
            "PlayPlugin should link playback session")
    require("media_sdk::platform_audio_macos" in cmake,
            "PlayPlugin should link macOS SDK audio platform implementation on Apple")


if __name__ == "__main__":
    main()
