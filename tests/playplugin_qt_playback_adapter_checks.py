#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    sdk_adapter_h = read("plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.h")
    sdk_adapter_cpp = read("plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp")
    pipeline_h = read("plugins/PlayPlugin/src/playback/PlaybackPipeline.h")
    pipeline_cpp = read("plugins/PlayPlugin/src/playback/PlaybackPipeline.cpp")
    engine_h = read("plugins/PlayPlugin/src/playback/PlayerEngine.h")
    cmake = read("plugins/PlayPlugin/CMakeLists.txt")

    for removed in (
        "plugins/PlayPlugin/src/playback/QtPlaybackAdapter.h",
        "plugins/PlayPlugin/src/playback/QtPlaybackAdapter.cpp",
        "plugins/PlayPlugin/src/playback/PlaybackDataBridge.h",
        "plugins/PlayPlugin/src/playback/PlaybackDataBridge.cpp",
        "plugins/PlayPlugin/src/audio/AudioRenderer.h",
        "plugins/PlayPlugin/src/video/VideoRenderer.h",
        "plugins/PlayPlugin/src/video/VideoFrameScheduler.h",
        "plugins/PlayPlugin/src/sync/ClockSync.h",
        "plugins/PlayPlugin/src/common/FrameQueue.h",
    ):
        require(not (ROOT / removed).exists(), f"{removed} should be removed after SDK session switch")

    require("SessionEventBridge" in sdk_adapter_h
            and "media_sdk::session::ISessionEvents" in sdk_adapter_cpp,
            "SdkPlaybackAdapter should own a per-session SDK event bridge")
    require("std::unique_ptr<media_sdk::session::PlaybackSession>" in sdk_adapter_h,
            "SdkPlaybackAdapter should own PlaybackSession")
    require("onRuntimeDiagnostics" in sdk_adapter_cpp and "PlayPerf: sdk" in sdk_adapter_cpp,
            "SdkPlaybackAdapter should bridge SDK diagnostics to PlayPlugin logging")
    require("SessionEventBridge" in sdk_adapter_h + sdk_adapter_cpp,
            "SdkPlaybackAdapter should bind each PlaybackSession to an immutable event bridge")
    require("currentEventSerial()" not in sdk_adapter_cpp,
            "SdkPlaybackAdapter callbacks must not read the current global serial dynamically")

    for token in (
        "media_sdk::IDecodeFrameSink",
        "media_sdk::IEventSink",
        "media_sdk::runtime::IRuntimePlayerEvents",
        "std::make_unique<media_sdk::Player>",
        "std::make_shared<media_sdk::runtime::RuntimePlayer>",
        "AudioFrameEvent",
        "VideoFrameEvent",
        "float32InterleavedSamples",
    ):
        require(token not in sdk_adapter_h + sdk_adapter_cpp,
                f"SdkPlaybackAdapter should not depend on old orchestration token: {token}")

    require("std::unique_ptr<SdkPlaybackAdapter>" in pipeline_h,
            "PlaybackPipeline should own only the SDK adapter for playback")
    require("QtRhiVideoPresenter" in pipeline_h + pipeline_cpp,
            "PlaybackPipeline should keep only Qt presentation as PlayPlugin responsibility")
    require("CoreAudioAudioOutput" in pipeline_cpp,
            "PlaybackPipeline should inject the platform audio output into SDK session")
    require("PlaybackRuntimeMode" not in pipeline_h + pipeline_cpp + engine_h,
            "PlayPlugin should no longer expose a LegacyQt/SdkRuntime switch")

    for token in (
        "QtPlaybackAdapter",
        "PlaybackDataBridge",
        "AudioRenderer",
        "VideoRenderer",
        "VideoFrameScheduler",
        "ClockSync",
        "FrameQueue",
    ):
        require(token not in cmake, f"PlayPlugin CMake should not compile removed legacy component: {token}")

    require("media_sdk::playback_session" in cmake,
            "PlayPlugin should link the playback session SDK target")
    require("media_sdk::playback_runtime" in cmake,
            "PlayPlugin should link runtime interfaces for presenter/audio injection")


if __name__ == "__main__":
    main()
