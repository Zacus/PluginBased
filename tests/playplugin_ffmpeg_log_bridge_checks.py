#!/usr/bin/env python3

"""Architecture guard for keeping FFmpeg log callback setup out of FFmpegDecoder."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    bridge_h_path = ROOT / "plugins/PlayPlugin/src/decode/FFmpegLogBridge.h"
    bridge_cpp_path = ROOT / "plugins/PlayPlugin/src/decode/FFmpegLogBridge.cpp"
    require(bridge_h_path.exists(), "FFmpegLogBridge.h should exist")
    require(bridge_cpp_path.exists(), "FFmpegLogBridge.cpp should exist")

    decoder_cpp = read("plugins/PlayPlugin/src/FFmpegDecoder.cpp")
    bridge_h = read("plugins/PlayPlugin/src/decode/FFmpegLogBridge.h")
    bridge_cpp = read("plugins/PlayPlugin/src/decode/FFmpegLogBridge.cpp")
    cmake = read("plugins/PlayPlugin/CMakeLists.txt")
    root_cmake = read("CMakeLists.txt")

    require("Installs the FFmpeg global log callback used by PlayPlugin" in bridge_h,
            "FFmpegLogBridge.h should explain its file purpose")
    require("Adapts FFmpeg C log callbacks to the project logger" in bridge_cpp,
            "FFmpegLogBridge.cpp should explain its file purpose")
    require("void installFFmpegLogBridge()" in bridge_h,
            "FFmpegLogBridge should expose installFFmpegLogBridge")
    require("av_log_set_callback" in bridge_cpp and "[FFmpeg]" in bridge_cpp,
            "FFmpegLogBridge should own FFmpeg log callback setup")
    require("vsnprintf" in bridge_cpp and "level > AV_LOG_WARNING" in bridge_cpp,
            "FFmpegLogBridge should preserve existing log filtering and formatting")
    require("installFFmpegLogBridge();" in decoder_cpp,
            "FFmpegDecoder should install the log bridge")
    require("av_log_set_callback" not in decoder_cpp and "vsnprintf" not in decoder_cpp,
            "FFmpegDecoder should not contain FFmpeg log callback details")
    require("src/decode/FFmpegLogBridge.h" in cmake and
            "src/decode/FFmpegLogBridge.cpp" in cmake,
            "PlayPlugin CMake should compile FFmpegLogBridge")
    require("playplugin_ffmpeg_log_bridge_checks" in root_cmake,
            "CTest should run the FFmpeg log bridge check")


if __name__ == "__main__":
    main()
