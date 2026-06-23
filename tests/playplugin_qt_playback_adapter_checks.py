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
    bridge_h = read("plugins/PlayPlugin/src/playback/PlaybackDataBridge.h")
    bridge_cpp = read("plugins/PlayPlugin/src/playback/PlaybackDataBridge.cpp")
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
    require("PlaybackDataBridge" in adapter_h and "m_dataBridge" in adapter_cpp,
            "QtPlaybackAdapter should route data events through PlaybackDataBridge")
    require("QObject" not in bridge_h and "Q_OBJECT" not in bridge_h,
            "PlaybackDataBridge should not be a QObject or GUI-thread object")
    require("pushAudio(" in bridge_h and "pushVideo(" in bridge_h and "finish(" in bridge_h,
            "PlaybackDataBridge should expose audio/video/drain data-path methods")
    require("m_audioQueue->push(" in bridge_cpp and "m_videoQueue->push(" in bridge_cpp,
            "PlaybackDataBridge should use blocking queue push off the GUI thread")
    require("m_audioQueue->finish(" in bridge_cpp and "m_videoQueue->finish(" in bridge_cpp,
            "PlaybackDataBridge should use formal FrameQueue finish APIs for EOF")
    required_bridge_stats = (
        "PlaybackDataBridgeStats",
        "audioAccepted",
        "videoAccepted",
        "audioRejectedStale",
        "videoRejectedStale",
        "eofAccepted",
        "queueAbortFailures",
    )
    for token in required_bridge_stats:
        require(token in bridge_h + bridge_cpp,
                f"PlaybackDataBridge should expose diagnostic counter: {token}")
    require("PlayDataBridge: session=" in bridge_cpp,
            "PlaybackDataBridge should log one summary line for EOF/stop diagnostics")
    require("VideoFrameQueue" in adapter_h and "AudioFrameQueue" in adapter_h,
            "QtPlaybackAdapter should bridge SDK frames into existing PlayPlugin queues")
    require("if (m_paused)" not in adapter_cpp,
            "QtPlaybackAdapter should not drop decoded data events while playback is paused")
    handle_start = adapter_cpp.find("void QtPlaybackAdapter::handleEvent")
    make_audio_start = adapter_cpp.find("AVFramePtr QtPlaybackAdapter::makeAudioFrame")
    handle_event_body = adapter_cpp[handle_start:make_audio_start]
    require("AudioFrameEvent" not in handle_event_body and "VideoFrameEvent" not in handle_event_body,
            "QtPlaybackAdapter::handleEvent should not enqueue audio/video frames on the GUI thread")
    require("dropped audio frame because queue is full" not in adapter_cpp and
            "dropped video frame because queue is full" not in adapter_cpp,
            "QtPlaybackAdapter should not silently drop data frames when queues are full")
    eof_block = adapter_cpp[adapter_cpp.find("EndOfFileEvent"):
                            adapter_cpp.find("PositionChangedEvent")]
    require("m_dataBridge.finish" in adapter_cpp and ".flush()" not in eof_block,
            "QtPlaybackAdapter should route EOF through the data bridge without flushing queues")
    require("AudioFrameEvent" in adapter_cpp and "VideoFrameEvent" in adapter_cpp,
            "QtPlaybackAdapter should handle SDK audio and video frame events")
    require("int videoRowBytes(media_sdk::PixelFormat format, int width)" in adapter_cpp and
            "return width;" in adapter_cpp and
            "(width + 1) / 2" not in adapter_cpp[adapter_cpp.find("int videoRowBytes"):
                                                  adapter_cpp.find("} // namespace")],
            "QtPlaybackAdapter should copy each SDK plane using that plane's byte width")
    require("mediaInfoReady" in adapter_h and "endOfFile" in adapter_h and "seekCompleted" in adapter_h,
            "QtPlaybackAdapter should expose decoder-compatible Qt signals")

    require("src/playback/PlaybackDataBridge.h" in cmake and
            "src/playback/PlaybackDataBridge.cpp" in cmake,
            "PlayPlugin CMake should compile PlaybackDataBridge")
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
