#!/usr/bin/env python3

"""B+ completion guard for keeping PlayPlugin on the media SDK playback core."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_before(text: str, first: str, second: str, message: str) -> None:
    first_index = text.find(first)
    second_index = text.find(second)
    require(first_index >= 0 and second_index >= 0 and first_index < second_index, message)


def main() -> None:
    root_cmake = read("CMakeLists.txt")
    plugin_cmake = read("plugins/PlayPlugin/CMakeLists.txt")
    flow_doc = read("plugins/PlayPlugin/PlayPluginExecutionFlow.md")
    frame_queue = read("plugins/PlayPlugin/src/common/FrameQueue.h")
    adapter_h = read("plugins/PlayPlugin/src/playback/QtPlaybackAdapter.h")
    adapter_cpp = read("plugins/PlayPlugin/src/playback/QtPlaybackAdapter.cpp")
    bridge_cpp = read("plugins/PlayPlugin/src/playback/PlaybackDataBridge.cpp")
    pipeline_cpp = read("plugins/PlayPlugin/src/playback/PlaybackPipeline.cpp")

    decode_dir = ROOT / "plugins" / "PlayPlugin" / "src" / "decode"
    require(not decode_dir.exists(),
            "PlayPlugin should not keep the old Qt-bound decode core after B+ migration")

    migrated_sources = (
        "FFmpegDecoder",
        "MediaOpener",
        "StreamFrameDecoder",
        "VideoFrameProcessor",
        "DecodePerformance",
        "DecodeLoopControl",
        "FFmpegLogBridge",
        "HardwareDecoderFactory",
        "HardwareDecoderBackend",
        "VideoToolboxBackend",
        "D3D11VABackend",
        "VaapiBackend",
    )
    for token in migrated_sources:
        require(token not in plugin_cmake,
                f"PlayPlugin CMake should not compile migrated decode implementation: {token}")

    obsolete_tests = (
        "playplugin_decoder_decoupling_checks",
        "playplugin_media_opener_checks",
        "playplugin_stream_frame_decoder_checks",
        "playplugin_video_frame_processor_checks",
        "playplugin_ffmpeg_log_bridge_checks",
        "playplugin_decode_loop_control_checks",
    )
    for test_name in obsolete_tests:
        require(f"add_test(NAME {test_name}" not in root_cmake,
                f"CTest should not register obsolete FFmpegDecoder split test: {test_name}")

    require("add_test(NAME playplugin_bplus_completion_checks" in root_cmake,
            "CTest should run the B+ completion guard")
    require("QtPlaybackAdapter" in plugin_cmake and "media_sdk_core" in plugin_cmake,
            "PlayPlugin should compile the Qt SDK adapter and link media_sdk_core")
    require("media_sdk::Player" in read("plugins/PlayPlugin/src/playback/QtPlaybackAdapter.h"),
            "QtPlaybackAdapter should be the PlayPlugin boundary to the media SDK player")

    forbidden_current_flow = (
        "FFmpegDecoder 是",
        "participant Decoder as FFmpegDecoder",
        "MediaOpener",
        "StreamFrameDecoder",
        "DecodeLoopControl",
        "PlaybackSeekCoordinator",
        "m_decoder->",
    )
    for token in forbidden_current_flow:
        require(token not in flow_doc,
                f"Execution flow should describe the SDK-driven current path, not old decode flow: {token}")

    required_current_flow = (
        "media_sdk::Player",
        "QtPlaybackAdapter",
        "DecodeWorker",
        "std::jthread",
        "IEventSink",
        "AudioFrameQueue",
        "VideoFrameQueue",
    )
    for token in required_current_flow:
        require(token in flow_doc,
                f"Execution flow should document the B+ playback path token: {token}")

    require("bool finish(int serial = 0)" in frame_queue,
            "FrameQueue should expose a formal blocking EOF/drain API")
    require("bool tryFinish(int serial = 0)" in frame_queue,
            "FrameQueue should expose a non-blocking EOF/drain API for explicit retry policies")
    require("return push(T {}, serial, true);" in frame_queue,
            "FrameQueue::finish should use blocking push so EOF preserves accepted-frame order")
    require("return tryPush(T {}, serial, true);" in frame_queue,
            "FrameQueue::tryFinish should make non-blocking EOF retry policies explicit")

    forbidden_adapter_eof_retry = (
        "m_pendingAudioEof",
        "m_pendingVideoEof",
        "m_eofRetryScheduled",
        "tryPushPendingEof",
        "scheduleEofRetry",
        "QTimer::singleShot",
        "tryPush(nullptr",
        "dropped audio frame because queue is full",
        "dropped video frame because queue is full",
    )
    for token in forbidden_adapter_eof_retry:
        require(token not in adapter_h and token not in adapter_cpp,
                f"QtPlaybackAdapter should not keep temporary EOF retry/drop path: {token}")

    require("m_videoQueue->abort();" in bridge_cpp and "m_audioQueue->abort();" in bridge_cpp,
            "PlaybackDataBridge::cancel should abort queues to wake blocked data producers")
    open_body = adapter_cpp[adapter_cpp.find("void QtPlaybackAdapter::openFile"):
                            adapter_cpp.find("void QtPlaybackAdapter::setPaused")]
    require_before(open_body,
                   "m_dataBridge.cancel();",
                   "resetPlayer();",
                   "QtPlaybackAdapter::openFile should cancel the old data path before resetting player")
    require_before(open_body,
                   "m_videoQueue->flush();",
                   "m_videoQueue->resetAbort();",
                   "QtPlaybackAdapter::openFile should flush before resetting video queue abort state")
    require_before(open_body,
                   "m_audioQueue->flush();",
                   "m_audioQueue->resetAbort();",
                   "QtPlaybackAdapter::openFile should flush before resetting audio queue abort state")
    seek_body = adapter_cpp[adapter_cpp.find("void QtPlaybackAdapter::seekTo"):
                            adapter_cpp.find("void QtPlaybackAdapter::stopDecoding")]
    require_before(seek_body,
                   "m_dataBridge.cancelGeneration();",
                   "m_player->seek",
                   "QtPlaybackAdapter::seekTo should cancel only the old data generation before submitting seek")
    require_before(seek_body,
                   "m_videoQueue->flush();",
                   "m_videoQueue->resetAbort();",
                   "QtPlaybackAdapter::seekTo should flush before resetting video queue abort state")
    require_before(seek_body,
                   "m_audioQueue->flush();",
                   "m_audioQueue->resetAbort();",
                   "QtPlaybackAdapter::seekTo should flush before resetting audio queue abort state")
    stop_body = pipeline_cpp[pipeline_cpp.find("void PlaybackPipeline::stopComponents"):
                             pipeline_cpp.find("void PlaybackPipeline::seek")]
    require_before(stop_body,
                   "m_adapter->stopDecoding();",
                   "m_audioRenderer->stopRenderer();",
                   "PlaybackPipeline::stopComponents should cancel adapter/data path before waiting for renderers")
    data_event_body = adapter_cpp[adapter_cpp.find("bool QtPlaybackAdapter::handleDataEvent"):
                                  adapter_cpp.find("void QtPlaybackAdapter::handleEvent")]
    require_before(data_event_body,
                   "SeekCompletedEvent",
                   "m_dataBridge.setGeneration",
                   "QtPlaybackAdapter should restore bridge generation in the SDK callback path on seek completion")


if __name__ == "__main__":
    main()
