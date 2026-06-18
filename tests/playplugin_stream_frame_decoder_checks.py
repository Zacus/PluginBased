#!/usr/bin/env python3

"""Architecture guard for moving packet send/frame receive logic out of FFmpegDecoder."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    helper_h_path = ROOT / "plugins/PlayPlugin/src/decode/StreamFrameDecoder.h"
    helper_cpp_path = ROOT / "plugins/PlayPlugin/src/decode/StreamFrameDecoder.cpp"
    require(helper_h_path.exists(), "StreamFrameDecoder.h should exist")
    require(helper_cpp_path.exists(), "StreamFrameDecoder.cpp should exist")

    decoder_h = read("plugins/PlayPlugin/src/decode/FFmpegDecoder.h")
    decoder_cpp = read("plugins/PlayPlugin/src/decode/FFmpegDecoder.cpp")
    helper_h = read("plugins/PlayPlugin/src/decode/StreamFrameDecoder.h")
    helper_cpp = read("plugins/PlayPlugin/src/decode/StreamFrameDecoder.cpp")
    cmake = read("plugins/PlayPlugin/CMakeLists.txt")
    root_cmake = read("CMakeLists.txt")

    require("向 FFmpeg 编解码器发送包，并回调输出已归一化时间戳的解码帧" in helper_h,
            "StreamFrameDecoder.h should explain its file purpose")
    require("实现包发送、帧接收和时间戳归一化" in helper_cpp,
            "StreamFrameDecoder.cpp should explain its file purpose")
    require("class StreamFrameDecoder" in helper_h,
            "StreamFrameDecoder should be declared")
    require("using FrameHandler" in helper_h and "bool sendPacket" in helper_h,
            "StreamFrameDecoder should expose a callback-based packet decode API")
    require("avcodec_send_packet" in helper_cpp and "avcodec_receive_frame" in helper_cpp,
            "StreamFrameDecoder should own FFmpeg packet/frame decode calls")
    require("best_effort_timestamp" in helper_cpp and "av_rescale_q" in helper_cpp,
            "StreamFrameDecoder should normalize decoded frame PTS to microseconds")
    require("StreamFrameDecoder m_streamFrameDecoder" in decoder_h,
            "FFmpegDecoder should own a StreamFrameDecoder value member")
    require("m_streamFrameDecoder.sendPacket" in decoder_cpp,
            "FFmpegDecoder should delegate packet/frame decoding")
    require("avcodec_send_packet" not in decoder_cpp and
            "avcodec_receive_frame" not in decoder_cpp and
            "best_effort_timestamp" not in decoder_cpp,
            "FFmpegDecoder should not perform packet/frame receive details directly")
    require("src/decode/StreamFrameDecoder.h" in cmake and
            "src/decode/StreamFrameDecoder.cpp" in cmake,
            "PlayPlugin CMake should compile StreamFrameDecoder")
    require("playplugin_stream_frame_decoder_checks" in root_cmake,
            "CTest should run the stream frame decoder check")


if __name__ == "__main__":
    main()
