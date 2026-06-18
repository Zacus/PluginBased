#!/usr/bin/env python3

"""Architecture guard for gradually splitting FFmpegDecoder responsibilities."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    perf_h_path = ROOT / "plugins/PlayPlugin/src/decode/DecodePerformance.h"
    perf_cpp_path = ROOT / "plugins/PlayPlugin/src/decode/DecodePerformance.cpp"
    require(perf_h_path.exists(), "DecodePerformance.h should exist")
    require(perf_cpp_path.exists(), "DecodePerformance.cpp should exist")

    decoder_h = read("plugins/PlayPlugin/src/decode/FFmpegDecoder.h")
    decoder_cpp = read("plugins/PlayPlugin/src/decode/FFmpegDecoder.cpp")
    perf_h = read("plugins/PlayPlugin/src/decode/DecodePerformance.h")
    perf_cpp = read("plugins/PlayPlugin/src/decode/DecodePerformance.cpp")
    cmake = read("plugins/PlayPlugin/CMakeLists.txt")
    root_cmake = read("CMakeLists.txt")

    require("记录带节流输出的 FFmpeg 解码性能计数" in perf_h,
            "DecodePerformance.h should explain its file purpose")
    require("实现带节流的解码性能日志输出" in perf_cpp,
            "DecodePerformance.cpp should explain its file purpose")
    require("struct DecodePerformanceStats" in perf_h,
            "DecodePerformance.h should own DecodePerformanceStats")
    require("class DecodePerformanceLogger" in perf_h,
            "DecodePerformance.h should declare DecodePerformanceLogger")
    require("PerformanceLogIntervalMs" in perf_cpp and "PlayPerf: decoder" in perf_cpp,
            "DecodePerformance.cpp should own throttled PlayPerf decoder logging")
    require("pixelFormatName" in perf_cpp and "averageUs" in perf_cpp,
            "DecodePerformance.cpp should own decode performance formatting helpers")
    require("struct DecodePerformanceStats" not in decoder_h,
            "FFmpegDecoder should not define DecodePerformanceStats inline")
    require("QElapsedTimer m_decodePerfLogTimer" not in decoder_h,
            "FFmpegDecoder should not own the performance log timer directly")
    require("DecodePerformanceLogger m_decodePerf" in decoder_h,
            "FFmpegDecoder should own DecodePerformanceLogger as a value member")
    require("m_decodePerf.stats()" in decoder_cpp,
            "FFmpegDecoder should mutate counters through DecodePerformanceLogger::stats()")
    require("m_decodePerf.maybeLog" in decoder_cpp and
            "void FFmpegDecoder::maybeLogDecodePerformance" not in decoder_cpp,
            "FFmpegDecoder should delegate throttled performance logging")
    require("src/decode/DecodePerformance.h" in cmake and
            "src/decode/DecodePerformance.cpp" in cmake,
            "PlayPlugin CMake should compile DecodePerformance")
    require("playplugin_decoder_decoupling_checks" in root_cmake,
            "CTest should run the decoder decoupling check")


if __name__ == "__main__":
    main()
