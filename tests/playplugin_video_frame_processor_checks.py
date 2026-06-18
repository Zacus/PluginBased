#!/usr/bin/env python3

"""Architecture guard for moving FFmpegDecoder video frame preparation into a focused processor."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    processor_h_path = ROOT / "plugins/PlayPlugin/src/decode/VideoFrameProcessor.h"
    processor_cpp_path = ROOT / "plugins/PlayPlugin/src/decode/VideoFrameProcessor.cpp"
    require(processor_h_path.exists(), "VideoFrameProcessor.h should exist")
    require(processor_cpp_path.exists(), "VideoFrameProcessor.cpp should exist")

    decoder_h = read("plugins/PlayPlugin/src/decode/FFmpegDecoder.h")
    decoder_cpp = read("plugins/PlayPlugin/src/decode/FFmpegDecoder.cpp")
    processor_h = read("plugins/PlayPlugin/src/decode/VideoFrameProcessor.h")
    processor_cpp = read("plugins/PlayPlugin/src/decode/VideoFrameProcessor.cpp")
    cmake = read("plugins/PlayPlugin/CMakeLists.txt")
    root_cmake = read("CMakeLists.txt")

    require("在解码视频帧进入渲染队列前完成预处理" in processor_h,
            "VideoFrameProcessor.h should explain its file purpose")
    require("实现硬件帧转 CPU 帧，以及像素格式归一化" in processor_cpp,
            "VideoFrameProcessor.cpp should explain its file purpose")
    require("class VideoFrameProcessor" in processor_h,
            "VideoFrameProcessor should be declared")
    require("AVFramePtr prepareForQueue" in processor_h,
            "VideoFrameProcessor should expose a single video preparation entry point")
    require("SwsContextPtr m_videoSwsCtx" in processor_h,
            "VideoFrameProcessor should own swscale cache state")
    require("m_hardwareTransferFailureCount" in processor_h,
            "VideoFrameProcessor should own hardware transfer failure throttling")
    require("transferHardwareFrameToCpu" in processor_cpp and
            "normalizeVideoFrame" in processor_cpp and
            "copyFrameMetadata" in processor_cpp,
            "VideoFrameProcessor should own transfer, normalization, and metadata copying")
    require("shouldPreserveHardwareFrameForDirectRender" in processor_cpp,
            "VideoFrameProcessor should own native direct-render preservation decisions")
    require("VideoFrameProcessor m_videoFrameProcessor" in decoder_h,
            "FFmpegDecoder should own a VideoFrameProcessor value member")
    for forbidden in (
            "SwsContextPtr      m_videoSwsCtx",
            "m_hardwareTransferFailureCount",
            "AVFramePtr prepareVideoFrameForQueue",
            "AVFramePtr transferHardwareFrameToCpu",
            "AVFramePtr normalizeVideoFrame",
            "void copyFrameMetadata"):
        require(forbidden not in decoder_h,
                f"FFmpegDecoder should not keep video frame processor detail: {forbidden}")
    require("m_videoFrameProcessor.prepareForQueue" in decoder_cpp,
            "FFmpegDecoder should delegate decoded video frame preparation")
    require("AVFramePtr FFmpegDecoder::normalizeVideoFrame" not in decoder_cpp and
            "AVFramePtr FFmpegDecoder::transferHardwareFrameToCpu" not in decoder_cpp,
            "FFmpegDecoder should not implement video frame processing helpers")
    require("src/decode/VideoFrameProcessor.h" in cmake and
            "src/decode/VideoFrameProcessor.cpp" in cmake,
            "PlayPlugin CMake should compile VideoFrameProcessor")
    require("playplugin_video_frame_processor_checks" in root_cmake,
            "CTest should run the video frame processor check")


if __name__ == "__main__":
    main()
