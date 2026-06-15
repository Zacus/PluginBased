#!/usr/bin/env python3

"""Architecture guard for gradually splitting FFmpegSurface rendering responsibilities."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    format_h_path = ROOT / "plugins/PlayPlugin/src/render/VideoPixelFormat.h"
    format_cpp_path = ROOT / "plugins/PlayPlugin/src/render/VideoPixelFormat.cpp"
    require(format_h_path.exists(), "VideoPixelFormat.h should exist")
    require(format_cpp_path.exists(), "VideoPixelFormat.cpp should exist")

    surface_cpp = read("plugins/PlayPlugin/src/FFmpegSurface.cpp")
    format_h = read("plugins/PlayPlugin/src/render/VideoPixelFormat.h")
    format_cpp = read("plugins/PlayPlugin/src/render/VideoPixelFormat.cpp")
    cmake = read("plugins/PlayPlugin/CMakeLists.txt")
    root_cmake = read("CMakeLists.txt")

    require("Describes YUV/semiplanar video formats for FFmpegSurface rendering" in format_h,
            "VideoPixelFormat.h should explain its file purpose")
    require("Implements FFmpeg pixel format mapping for QRhi texture uploads" in format_cpp,
            "VideoPixelFormat.cpp should explain its file purpose")
    require("#include \"render/VideoPixelFormat.h\"" in surface_cpp,
            "FFmpegSurface should include the extracted pixel format helper")
    require("struct PixelFormatInfo" not in surface_cpp,
            "FFmpegSurface should not define PixelFormatInfo inline")
    require("enum class PlaneLayout" not in surface_cpp,
            "FFmpegSurface should not define PlaneLayout inline")
    require("struct PixelFormatInfo" in format_h and "enum class PlaneLayout" in format_h,
            "VideoPixelFormat.h should own pixel format types")
    require("PixelFormatInfo::fromAVFormat" in format_cpp,
            "VideoPixelFormat.cpp should own AVPixelFormat mapping")
    require("AV_PIX_FMT_NV12" in format_cpp and "AV_PIX_FMT_P010LE" in format_cpp,
            "VideoPixelFormat.cpp should preserve semiplanar format mapping")
    require("QRhiTexture::RG8" in format_cpp and "QRhiTexture::RG16" in format_cpp,
            "VideoPixelFormat.cpp should preserve two-channel chroma texture formats")
    require("src/render/VideoPixelFormat.h" in cmake and
            "src/render/VideoPixelFormat.cpp" in cmake,
            "PlayPlugin CMake should compile the extracted pixel format helper")
    require("playplugin_surface_decoupling_checks" in root_cmake,
            "CTest should run the Surface decoupling check")


if __name__ == "__main__":
    main()
