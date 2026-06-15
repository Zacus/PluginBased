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
    material_h_path = ROOT / "plugins/PlayPlugin/src/render/VideoMaterial.h"
    material_cpp_path = ROOT / "plugins/PlayPlugin/src/render/VideoMaterial.cpp"
    require(format_h_path.exists(), "VideoPixelFormat.h should exist")
    require(format_cpp_path.exists(), "VideoPixelFormat.cpp should exist")
    require(material_h_path.exists(), "VideoMaterial.h should exist")
    require(material_cpp_path.exists(), "VideoMaterial.cpp should exist")

    surface_cpp = read("plugins/PlayPlugin/src/FFmpegSurface.cpp")
    format_h = read("plugins/PlayPlugin/src/render/VideoPixelFormat.h")
    format_cpp = read("plugins/PlayPlugin/src/render/VideoPixelFormat.cpp")
    material_h = read("plugins/PlayPlugin/src/render/VideoMaterial.h")
    material_cpp = read("plugins/PlayPlugin/src/render/VideoMaterial.cpp")
    cmake = read("plugins/PlayPlugin/CMakeLists.txt")
    root_cmake = read("CMakeLists.txt")

    require("Describes YUV/semiplanar video formats for FFmpegSurface rendering" in format_h,
            "VideoPixelFormat.h should explain its file purpose")
    require("Implements FFmpeg pixel format mapping for QRhi texture uploads" in format_cpp,
            "VideoPixelFormat.cpp should explain its file purpose")
    require("#include \"render/VideoPixelFormat.h\"" in surface_cpp,
            "FFmpegSurface should include the extracted pixel format helper")
    require("#include \"render/VideoMaterial.h\"" in surface_cpp,
            "FFmpegSurface should include the extracted material helper")
    require("struct PixelFormatInfo" not in surface_cpp,
            "FFmpegSurface should not define PixelFormatInfo inline")
    require("enum class PlaneLayout" not in surface_cpp,
            "FFmpegSurface should not define PlaneLayout inline")
    for forbidden in ("class RhiTextureWrapper", "struct PendingUpload",
                      "class VideoMaterial", "class VideoShader"):
        require(forbidden not in surface_cpp,
                f"FFmpegSurface should not define material helper inline: {forbidden}")
    require("struct PixelFormatInfo" in format_h and "enum class PlaneLayout" in format_h,
            "VideoPixelFormat.h should own pixel format types")
    require("PixelFormatInfo::fromAVFormat" in format_cpp,
            "VideoPixelFormat.cpp should own AVPixelFormat mapping")
    require("AV_PIX_FMT_NV12" in format_cpp and "AV_PIX_FMT_P010LE" in format_cpp,
            "VideoPixelFormat.cpp should preserve semiplanar format mapping")
    require("QRhiTexture::RG8" in format_cpp and "QRhiTexture::RG16" in format_cpp,
            "VideoPixelFormat.cpp should preserve two-channel chroma texture formats")
    require("Owns the QSG material state used by FFmpegSurface video nodes" in material_h,
            "VideoMaterial.h should explain its file purpose")
    require("Implements the QSG material shader and QRhi texture upload path" in material_cpp,
            "VideoMaterial.cpp should explain its file purpose")
    require("class VideoMaterial" in material_h and "struct PendingUpload" in material_h,
            "VideoMaterial.h should own the material API and upload state")
    require("class VideoShader" in material_cpp and "class RhiTextureWrapper" in material_cpp,
            "VideoMaterial.cpp should own shader and texture wrapper internals")
    require("updateUniformData" in material_cpp and "updateSampledImage" in material_cpp,
            "VideoMaterial.cpp should keep QSG shader update logic")
    require("src/render/VideoPixelFormat.h" in cmake and
            "src/render/VideoPixelFormat.cpp" in cmake,
            "PlayPlugin CMake should compile the extracted pixel format helper")
    require("src/render/VideoMaterial.h" in cmake and
            "src/render/VideoMaterial.cpp" in cmake,
            "PlayPlugin CMake should compile the extracted material helper")
    require("playplugin_surface_decoupling_checks" in root_cmake,
            "CTest should run the Surface decoupling check")


if __name__ == "__main__":
    main()
