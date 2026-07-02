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
    require("ClockSync.cpp" not in sdk_cmake and "FrameScheduler.cpp" not in sdk_cmake,
            "media_sdk_core should not compile presenter playback clock/scheduler primitives")
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
    for token in ["HardwareDecoderFactory.cpp", "VideoToolboxBackend.cpp"]:
        require(token in sdk_cmake,
                f"media_sdk_core should compile SDK hardware decode backend source: {token}")
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

    result_header = read(PUBLIC_INCLUDE / "Result.h")
    require(result_header.count("class [[nodiscard") >= 2,
            "Result.h should mark both Result<T> and Result<void> nodiscard")
    for token in [
        "Result",
        "Result<void>",
    ]:
        require(token in result_header,
                f"Result.h should mark SDK result contracts nodiscard token: {token}")

    player_header = read(PUBLIC_INCLUDE / "Player.h")
    for token in [
        "[[nodiscard",
        "Result<void> open(const std::filesystem::path& path)",
        "Result<void> seek(std::chrono::milliseconds position)",
        "control",
    ]:
        require(token in player_header,
                f"Player.h should mark fallible SDK operations nodiscard token: {token}")

    decode_frame_sink_header = PUBLIC_INCLUDE / "DecodeFrameSink.h"
    require(decode_frame_sink_header.exists(),
            "DecodeFrameSink.h should define the dedicated decoded frame data channel")
    decode_frame_sink_text = read(decode_frame_sink_header)
    for token in ["class IDecodeFrameSink", "DecodeFramePushStatus", "DecodeFramePushResult"]:
        require(token in decode_frame_sink_text,
                f"DecodeFrameSink.h should expose frame channel contract token: {token}")

    frame_header = read(PUBLIC_INCLUDE / "Frame.h")
    for token in ["PlaneView", "NativeHandle", "VideoFrame", "AudioFrame",
                  "std::chrono::microseconds", "std::shared_ptr<void> storage"]:
        require(token in frame_header,
                f"Frame.h should expose the phase 3 frame contract token: {token}")

    media_events_path = PUBLIC_INCLUDE / "MediaEvents.h"
    media_events_header = read(media_events_path)
    for token in ["MediaInfo", "channelLayoutMask", "PositionChangedEvent", "EndOfFileEvent"]:
        require(token in media_events_header,
                f"MediaEvents.h should expose the phase 3 event token: {token}")
    for forbidden in ("AudioFrameEvent", "VideoFrameEvent"):
        require(forbidden not in media_events_header,
                f"MediaEvents.h must not expose frame payload {forbidden}")

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
        SDK_ROOT / "src" / "HardwareDecoderFactory.h",
        SDK_ROOT / "src" / "HardwareDecoderFactory.cpp",
        SDK_ROOT / "src" / "VideoToolboxBackend.h",
        SDK_ROOT / "src" / "VideoToolboxBackend.cpp",
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
    for token in ["isAvailableForCodec", "configureContext"]:
        require(token in hardware_backend_header,
                f"SDK hardware decoder backend should expose codec configuration token: {token}")

    hardware_factory_cpp = read(SDK_ROOT / "src" / "HardwareDecoderFactory.cpp")
    videotoolbox_cpp = read(SDK_ROOT / "src" / "VideoToolboxBackend.cpp")
    for token in ["createHardwareDecoderBackend", "VideoToolboxBackend"]:
        require(token in hardware_factory_cpp,
                f"HardwareDecoderFactory should create platform backend token: {token}")
    for token in ["AV_HWDEVICE_TYPE_VIDEOTOOLBOX", "av_hwdevice_ctx_create",
                  "AV_PIX_FMT_VIDEOTOOLBOX", "get_format", "hw_device_ctx",
                  "av_hwframe_transfer_data"]:
        require(token in videotoolbox_cpp,
                f"VideoToolboxBackend should configure FFmpeg hardware decode token: {token}")

    video_processor_cpp = read(SDK_ROOT / "src" / "VideoFrameProcessor.cpp")
    for token in ["AV_PIX_FMT_YUV420P", "AV_PIX_FMT_NV12", "AV_PIX_FMT_VIDEOTOOLBOX",
                  "ColorRange::Full", "ColorSpace::Bt709", "NativeHandleKind::VideoToolboxPixelBuffer"]:
        require(token in video_processor_cpp,
                f"VideoFrameProcessor should map video frame metadata token: {token}")

    decode_worker_header = read(SDK_ROOT / "src" / "DecodeWorker.h")
    decode_worker_cpp = read(SDK_ROOT / "src" / "DecodeWorker.cpp")
    demuxer_header = read(SDK_ROOT / "src" / "Demuxer.h")
    demuxer_cpp = read(SDK_ROOT / "src" / "Demuxer.cpp")
    playback_controller_header = read(SDK_ROOT / "src" / "PlaybackController.h")
    playback_controller_cpp = read(SDK_ROOT / "src" / "PlaybackController.cpp")
    for token in ["std::jthread", "std::stop_token", "IEventSink&"]:
        require(token in decode_worker_header + decode_worker_cpp,
                f"DecodeWorker should use phase 6 async worker token: {token}")
    decode_worker_text = decode_worker_header + decode_worker_cpp
    for token in ["StateChangedEvent", "EndOfFileEvent"]:
        require(token in decode_worker_text,
                f"DecodeWorker should integrate playback/event token: {token}")
    forbidden_worker_scheduling_tokens = [
        "ClockSync",
        "FrameScheduler",
        "FrameScheduleAction",
        "FrameScheduleDecision",
        "m_clock",
        "currentTime()",
        "setAudioClock",
        "setPaused",
        "SubmitLeadTime",
        "LateDropThreshold",
        "Action::Wait",
        "Action::Drop",
        "shouldDropVideoWhenFull",
        "m_videoQueue",
    ]
    for token in forbidden_worker_scheduling_tokens:
        require(token not in decode_worker_text,
                f"DecodeWorker should not own presenter scheduling/clock token: {token}")
    for token in ["makeEvent(AudioFrameEvent", "makeEvent(VideoFrameEvent"]:
        require(token not in decode_worker_cpp,
                f"DecodeWorker should not emit decoded frames through IEventSink: {token}")
    for token in [
        "DecodeFramePushDiagnostics",
        "recordFramePushResult",
        "m_framePushDiagnostics",
        "DecodeFramePushStatus::Backpressured",
        "maxWaitUs",
    ]:
        require(token in decode_worker_header + decode_worker_cpp,
                f"DecodeWorker should record decoded frame push diagnostics token: {token}")
    require("DemuxerOptions" in demuxer_header and
            "enableHardwareDecode" in demuxer_header and
            "enableHardwareDecode" in demuxer_cpp,
            "Demuxer should expose and honor hardware decode options")
    require("createHardwareDecoderBackend" in demuxer_cpp and
            "configureContext" in demuxer_cpp and
            "hardwareDecoder" in demuxer_header,
            "Demuxer should configure and retain SDK hardware decode backend")
    require("avcodec_free_context(&context)" in demuxer_cpp and
            "hardwareDecoder.reset()" in demuxer_cpp and
            "avcodec_open2(context, codec, nullptr)" in demuxer_cpp,
            "Demuxer should rebuild a pure software context when hardware codec open fails")
    require(".enableHardwareDecode = m_config.enableHardwareDecode" in decode_worker_cpp,
            "DecodeWorker should pass PlayerConfig::enableHardwareDecode into Demuxer")
    require("m_media.hardwareDecoder.get()" in decode_worker_cpp,
            "DecodeWorker should pass retained hardware backend to VideoFrameProcessor")
    require("coalescedSeekPosition" in decode_worker_header + decode_worker_cpp and
            "while (!m_commands.empty() && m_commands.front().type == CommandType::Seek)" in decode_worker_cpp,
            "DecodeWorker should coalesce consecutive queued seek commands before resuming decode")
    wait_command_start = decode_worker_cpp.find("bool DecodeWorker::waitForCommand")
    wait_command_end = decode_worker_cpp.find("bool DecodeWorker::tryTakeCommand")
    require(wait_command_start >= 0 and wait_command_end > wait_command_start,
            "DecodeWorker should define waitForCommand before tryTakeCommand")
    wait_command_body = decode_worker_cpp[wait_command_start:wait_command_end]
    require("m_cv.wait(lock" in wait_command_body,
            "DecodeWorker::waitForCommand should use command/close predicate wakeups")
    for token in ["wait_for", "std::chrono::milliseconds(10)"]:
        require(token not in wait_command_body,
                f"DecodeWorker::waitForCommand should not poll commands with {token}")
    require("QueuePolicy::shouldDropVideoWhenFull" not in decode_worker_cpp,
            "DecodeWorker should leave presenter backpressure/drop policy to the Qt adapter layer")
    require("DecodeWorker" in playback_controller_header + playback_controller_cpp and
            "submit" in playback_controller_cpp,
            "PlaybackController should submit commands to DecodeWorker")
    for token in ["makeInterleavedAudioSamples", "std::vector<std::byte> samples(totalBytes)",
                  "std::memcpy", "AudioFrame::fromOwnedSamples",
                  "publishedInterleavedAudioSampleFormat"]:
        require(token in decode_worker_cpp,
                f"DecodeWorker should use the optimized audio frame construction token: {token}")
    require("mapAudioSampleFormat" not in decode_worker_cpp + decode_worker_header,
            "DecodeWorker should not hide planar-to-interleaved publication behind a generic format mapper name")
    require("samples.insert(" not in decode_worker_cpp and
            "std::make_shared<std::vector<std::byte>>(std::move(samples))" not in decode_worker_cpp,
            "DecodeWorker should not rebuild audio frames through per-sample vector inserts or shared vector copies")

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
