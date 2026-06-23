#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SDK_ROOT = ROOT / "sdk" / "media_core"
PUBLIC_INCLUDE = SDK_ROOT / "include" / "media_sdk"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    root_cmake = read(ROOT / "CMakeLists.txt")
    design_doc = read(ROOT / "docs" / "superpowers" / "specs" /
                      "2026-06-22-media-sdk-bplus-design.md")
    require((SDK_ROOT / "CMakeLists.txt").exists(),
            "sdk/media_core/CMakeLists.txt should exist")
    sdk_cmake = read(SDK_ROOT / "CMakeLists.txt")

    require("add_subdirectory(sdk/media_core)" in root_cmake,
            "top-level CMake should include the media SDK core target")
    require("add_test(NAME media_sdk_core_architecture_checks" in root_cmake,
            "CTest should run the media SDK core architecture check")
    require("add_library(media_sdk_core" in sdk_cmake,
            "media_sdk_core should be an independent library target")
    require("cxx_std_20" in sdk_cmake,
            "media_sdk_core should require C++20")
    require("AUTOMOC OFF" in sdk_cmake and "AUTOUIC OFF" in sdk_cmake and "AUTORCC OFF" in sdk_cmake,
            "media_sdk_core should disable Qt automoc/autouic/autorcc")
    require("ClockSync.cpp" in sdk_cmake and "FrameScheduler.cpp" in sdk_cmake,
            "media_sdk_core should compile the phase 2 primitive sources")
    require("MediaSdkCorePrimitivesTest" in sdk_cmake and "media_sdk_core_primitives" in sdk_cmake,
            "media_sdk_core should register primitive C++ tests")
    require("MediaSdkCoreFrameContractTest" in sdk_cmake and "media_sdk_core_frame_contract" in sdk_cmake,
            "media_sdk_core should register frame contract C++ tests")
    require("MediaSdkCoreFfmpegIntegrationTest" in sdk_cmake and "media_sdk_core_ffmpeg_integration" in sdk_cmake,
            "media_sdk_core should register FFmpeg integration tests")
    require("MediaSdkCoreVideoFrameProcessorTest" in sdk_cmake and "media_sdk_core_video_frame_processor" in sdk_cmake,
            "media_sdk_core should register video frame processor tests")
    require("MediaSdkCorePlaybackWorkerTest" in sdk_cmake and "media_sdk_core_playback_worker" in sdk_cmake,
            "media_sdk_core should register playback worker tests")
    require("FFmpeg::all" in sdk_cmake,
            "media_sdk_core should link FFmpeg for internal demux/decode support")
    require("FFmpegUtils.h" in sdk_cmake and "Demuxer.cpp" in sdk_cmake and
            "StreamDecoder.cpp" in sdk_cmake and "DecodePerformance.cpp" in sdk_cmake,
            "media_sdk_core should compile phase 4 FFmpeg internal sources")
    require("VideoFrameProcessor.cpp" in sdk_cmake and "HardwareDecoderBackend.h" in sdk_cmake,
            "media_sdk_core should compile phase 5 video processing internals")
    require("DecodeWorker.cpp" in sdk_cmake and "PlaybackController.cpp" in sdk_cmake and
            "QueuePolicy.h" in sdk_cmake,
            "media_sdk_core should compile phase 6 playback worker internals")

    expected_headers = [
        "PlayerConfig.h",
        "Error.h",
        "Result.h",
        "Frame.h",
        "MediaEvents.h",
        "Player.h",
    ]
    for header in expected_headers:
        require((PUBLIC_INCLUDE / header).exists(),
                f"missing public SDK header: {header}")

    frame_header = read(PUBLIC_INCLUDE / "Frame.h")
    for token in ["PlaneView", "NativeHandle", "VideoFrame", "AudioFrame",
                  "std::chrono::microseconds", "std::shared_ptr<void> storage"]:
        require(token in frame_header,
                f"Frame.h should expose the phase 3 frame contract token: {token}")

    media_events_header = read(PUBLIC_INCLUDE / "MediaEvents.h")
    for token in ["MediaInfo", "channelLayoutMask", "PositionChangedEvent", "AudioFrameEvent",
                  "VideoFrameEvent", "EndOfFileEvent"]:
        require(token in media_events_header,
                f"MediaEvents.h should expose the phase 3 event token: {token}")

    phase4_sources = [
        SDK_ROOT / "src" / "FFmpegUtils.h",
        SDK_ROOT / "src" / "Demuxer.h",
        SDK_ROOT / "src" / "Demuxer.cpp",
        SDK_ROOT / "src" / "StreamDecoder.h",
        SDK_ROOT / "src" / "StreamDecoder.cpp",
        SDK_ROOT / "src" / "DecodePerformance.h",
        SDK_ROOT / "src" / "DecodePerformance.cpp",
    ]
    for source in phase4_sources:
        require(source.exists(),
                f"missing phase 4 SDK internal source: {source.relative_to(ROOT)}")

    phase5_sources = [
        SDK_ROOT / "src" / "HardwareDecoderBackend.h",
        SDK_ROOT / "src" / "VideoFrameProcessor.h",
        SDK_ROOT / "src" / "VideoFrameProcessor.cpp",
    ]
    for source in phase5_sources:
        require(source.exists(),
                f"missing phase 5 SDK internal source: {source.relative_to(ROOT)}")

    phase6_sources = [
        SDK_ROOT / "src" / "DecodeWorker.h",
        SDK_ROOT / "src" / "DecodeWorker.cpp",
        SDK_ROOT / "src" / "PlaybackController.h",
        SDK_ROOT / "src" / "PlaybackController.cpp",
        SDK_ROOT / "src" / "QueuePolicy.h",
    ]
    for source in phase6_sources:
        require(source.exists(),
                f"missing phase 6 SDK internal source: {source.relative_to(ROOT)}")

    hardware_backend_header = read(SDK_ROOT / "src" / "HardwareDecoderBackend.h")
    require("std::string_view name() const" in hardware_backend_header,
            "SDK hardware decoder backend names should use std::string_view, not QString")

    video_processor_cpp = read(SDK_ROOT / "src" / "VideoFrameProcessor.cpp")
    for token in ["AV_PIX_FMT_YUV420P", "AV_PIX_FMT_NV12", "AV_PIX_FMT_VIDEOTOOLBOX",
                  "ColorRange::Full", "ColorSpace::Bt709", "NativeHandleKind::VideoToolboxPixelBuffer"]:
        require(token in video_processor_cpp,
                f"VideoFrameProcessor should map video frame metadata token: {token}")

    decode_worker_header = read(SDK_ROOT / "src" / "DecodeWorker.h")
    decode_worker_cpp = read(SDK_ROOT / "src" / "DecodeWorker.cpp")
    playback_controller_header = read(SDK_ROOT / "src" / "PlaybackController.h")
    playback_controller_cpp = read(SDK_ROOT / "src" / "PlaybackController.cpp")
    for token in ["std::jthread", "std::stop_token", "IEventSink&"]:
        require(token in decode_worker_header + decode_worker_cpp,
                f"DecodeWorker should use phase 6 async worker token: {token}")
    decode_worker_text = decode_worker_header + decode_worker_cpp
    for token in ["ClockSync", "StateChangedEvent", "EndOfFileEvent"]:
        require(token in decode_worker_text,
                f"DecodeWorker should integrate playback/event token: {token}")
    require("FrameScheduler::decide" not in decode_worker_cpp and
            "m_videoQueue" not in decode_worker_cpp,
            "DecodeWorker should not pre-schedule or drop video before the Qt presenter")
    require("emitEvent({ VideoFrameEvent { std::move(frame) } })" in decode_worker_cpp,
            "DecodeWorker should emit decoded video frames directly to the presenter boundary")
    require("QueuePolicy::shouldDropVideoWhenFull" not in decode_worker_cpp,
            "DecodeWorker should leave presenter backpressure/drop policy to the Qt adapter layer")
    require("DecodeWorker" in playback_controller_header + playback_controller_cpp and
            "submit" in playback_controller_cpp,
            "PlaybackController should submit commands to DecodeWorker")
    require("for (int sample = 0; sample < frame->nb_samples; ++sample)" in decode_worker_cpp and
            "planeData + sample * bytesPerSample" in decode_worker_cpp,
            "DecodeWorker should interleave planar audio samples before publishing SDK AudioFrame")

    forbidden_tokens = [
        "#include <Q",
        "#include <Qt",
        "QObject",
        "QThread",
        "QString",
        "QUrl",
        "QRhi",
        "QSG",
        "QAudio",
        "EventBus",
        "Observable",
        "template<class Derived>",
        "template<typename Derived>",
    ]
    for source in list(SDK_ROOT.rglob("*.h")) + list(SDK_ROOT.rglob("*.cpp")):
        text = read(source)
        for token in forbidden_tokens:
            require(token not in text,
                    f"{source.relative_to(ROOT)} must stay Qt-free; found {token}")

    required_design_terms = [
        "PIMPL",
        "single `IEventSink`",
        "不引入泛型 Observable",
        "不使用 CRTP",
        "command submission",
        "std::jthread",
        "std::stop_token",
    ]
    for term in required_design_terms:
        require(term in design_doc,
                f"media SDK design should document the constraint: {term}")


if __name__ == "__main__":
    main()
