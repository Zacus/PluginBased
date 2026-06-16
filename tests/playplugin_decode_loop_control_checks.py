#!/usr/bin/env python3

"""Architecture guard for isolating FFmpegDecoder decode-loop command control."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    control_h_path = ROOT / "plugins/PlayPlugin/src/decode/DecodeLoopControl.h"
    control_cpp_path = ROOT / "plugins/PlayPlugin/src/decode/DecodeLoopControl.cpp"
    require(control_h_path.exists(), "DecodeLoopControl.h should exist")
    require(control_cpp_path.exists(), "DecodeLoopControl.cpp should exist")

    decoder_h = read("plugins/PlayPlugin/src/FFmpegDecoder.h")
    decoder_cpp = read("plugins/PlayPlugin/src/FFmpegDecoder.cpp")
    control_h = read("plugins/PlayPlugin/src/decode/DecodeLoopControl.h")
    control_cpp = read("plugins/PlayPlugin/src/decode/DecodeLoopControl.cpp")
    cmake = read("plugins/PlayPlugin/CMakeLists.txt")
    root_cmake = read("CMakeLists.txt")

    require("Consumes decode-loop command state guarded by FFmpegDecoder mutexes" in control_h,
            "DecodeLoopControl.h should explain its file purpose")
    require("Implements locked command-state consumption for FFmpegDecoder" in control_cpp,
            "DecodeLoopControl.cpp should explain its file purpose")
    require("struct PendingSeekRequest" in control_h and "class DecodeLoopControl" in control_h,
            "DecodeLoopControl should expose a structured pending seek result")
    require("enum class EofWaitDecision" in control_h and "struct EofWaitResult" in control_h,
            "DecodeLoopControl should expose a structured EOF wait result")
    require("consumeSeekRequest" in control_h and "QMutexLocker" in control_cpp,
            "DecodeLoopControl should consume seek requests under the existing mutex")
    require("waitAfterEof" in control_h and "waitAfterEof" in control_cpp,
            "DecodeLoopControl should own EOF wait decision logic")
    require("openCondition.wait(&openMutex, 10)" in control_cpp,
            "DecodeLoopControl should wait briefly for new open requests after EOF")
    require("DecodeLoopControl m_decodeLoopControl" in decoder_h,
            "FFmpegDecoder should own DecodeLoopControl as a value member")
    require(decoder_cpp.count("m_decodeLoopControl.consumeSeekRequest") == 1,
            "FFmpegDecoder should only consume normal-loop seek requests directly")
    require("m_decodeLoopControl.waitAfterEof" in decoder_cpp,
            "FFmpegDecoder should delegate EOF waiting to DecodeLoopControl")
    require("m_seekRequested = false" not in decoder_cpp,
            "FFmpegDecoder should not clear pending seek requests directly")
    require("bool openRequested = false" not in decoder_cpp and
            "m_openCond.wait(&m_openMutex, 10)" not in decoder_cpp,
            "FFmpegDecoder should not own EOF open-wait polling")
    require("src/decode/DecodeLoopControl.h" in cmake and
            "src/decode/DecodeLoopControl.cpp" in cmake,
            "PlayPlugin CMake should compile DecodeLoopControl")
    require("playplugin_decode_loop_control_checks" in root_cmake,
            "CTest should run the decode loop control check")


if __name__ == "__main__":
    main()
