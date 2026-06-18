#!/usr/bin/env python3

"""Architecture guard for moving FFmpeg media opening and stream discovery out of FFmpegDecoder."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    opener_h_path = ROOT / "plugins/PlayPlugin/src/decode/MediaOpener.h"
    opener_cpp_path = ROOT / "plugins/PlayPlugin/src/decode/MediaOpener.cpp"
    require(opener_h_path.exists(), "MediaOpener.h should exist")
    require(opener_cpp_path.exists(), "MediaOpener.cpp should exist")

    decoder_h = read("plugins/PlayPlugin/src/decode/FFmpegDecoder.h")
    decoder_cpp = read("plugins/PlayPlugin/src/decode/FFmpegDecoder.cpp")
    opener_h = read("plugins/PlayPlugin/src/decode/MediaOpener.h")
    opener_cpp = read("plugins/PlayPlugin/src/decode/MediaOpener.cpp")
    cmake = read("plugins/PlayPlugin/CMakeLists.txt")
    root_cmake = read("CMakeLists.txt")

    require("打开 FFmpeg 媒体输入，并发现可解码的视频/音频流" in opener_h,
            "MediaOpener.h should explain its file purpose")
    require("实现媒体打开、流选择和编解码器上下文初始化" in opener_cpp,
            "MediaOpener.cpp should explain its file purpose")
    require("struct OpenedMedia" in opener_h and "struct MediaOpenResult" in opener_h,
            "MediaOpener should return a structured move-only media open result")
    require("class MediaOpener" in opener_h and "MediaOpenResult open(const QString& path)" in opener_h,
            "MediaOpener should expose an open entry point")
    require("createHardwareDecoderBackend" in opener_cpp and "openVideoCodec" in opener_cpp,
            "MediaOpener should own hardware-aware video codec opening")
    require("avformat_open_input" in opener_cpp and "av_find_best_stream" in opener_cpp,
            "MediaOpener should own FFmpeg input open and stream discovery")
    require("MediaOpener m_mediaOpener" in decoder_h,
            "FFmpegDecoder should own a MediaOpener value member")
    require("m_mediaOpener.open(path)" in decoder_cpp,
            "FFmpegDecoder should delegate media opening")
    require("avformat_open_input" not in decoder_cpp and "av_find_best_stream" not in decoder_cpp,
            "FFmpegDecoder should not perform FFmpeg input open or stream discovery directly")
    require("bool openVideoCodec" not in decoder_h and
            "AVCodecContext* createVideoCodecContext" not in decoder_h,
            "FFmpegDecoder should not expose codec-opening helper declarations")
    require("src/decode/MediaOpener.h" in cmake and
            "src/decode/MediaOpener.cpp" in cmake,
            "PlayPlugin CMake should compile MediaOpener")
    require("playplugin_media_opener_checks" in root_cmake,
            "CTest should run the media opener check")


if __name__ == "__main__":
    main()
