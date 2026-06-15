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
    node_h_path = ROOT / "plugins/PlayPlugin/src/render/VideoNode.h"
    node_cpp_path = ROOT / "plugins/PlayPlugin/src/render/VideoNode.cpp"
    geometry_h_path = ROOT / "plugins/PlayPlugin/src/render/VideoSurfaceGeometry.h"
    geometry_cpp_path = ROOT / "plugins/PlayPlugin/src/render/VideoSurfaceGeometry.cpp"
    require(format_h_path.exists(), "VideoPixelFormat.h should exist")
    require(format_cpp_path.exists(), "VideoPixelFormat.cpp should exist")
    require(material_h_path.exists(), "VideoMaterial.h should exist")
    require(material_cpp_path.exists(), "VideoMaterial.cpp should exist")
    require(node_h_path.exists(), "VideoNode.h should exist")
    require(node_cpp_path.exists(), "VideoNode.cpp should exist")
    require(geometry_h_path.exists(), "VideoSurfaceGeometry.h should exist")
    require(geometry_cpp_path.exists(), "VideoSurfaceGeometry.cpp should exist")

    surface_cpp = read("plugins/PlayPlugin/src/FFmpegSurface.cpp")
    format_h = read("plugins/PlayPlugin/src/render/VideoPixelFormat.h")
    format_cpp = read("plugins/PlayPlugin/src/render/VideoPixelFormat.cpp")
    material_h = read("plugins/PlayPlugin/src/render/VideoMaterial.h")
    material_cpp = read("plugins/PlayPlugin/src/render/VideoMaterial.cpp")
    node_h = read("plugins/PlayPlugin/src/render/VideoNode.h")
    node_cpp = read("plugins/PlayPlugin/src/render/VideoNode.cpp")
    geometry_h = read("plugins/PlayPlugin/src/render/VideoSurfaceGeometry.h")
    geometry_cpp = read("plugins/PlayPlugin/src/render/VideoSurfaceGeometry.cpp")
    cmake = read("plugins/PlayPlugin/CMakeLists.txt")
    root_cmake = read("CMakeLists.txt")

    require("Describes YUV/semiplanar video formats for FFmpegSurface rendering" in format_h,
            "VideoPixelFormat.h should explain its file purpose")
    require("Implements FFmpeg pixel format mapping for QRhi texture uploads" in format_cpp,
            "VideoPixelFormat.cpp should explain its file purpose")
    require("#include \"render/VideoNode.h\"" in surface_cpp,
            "FFmpegSurface should include the extracted video node helper")
    require("#include \"render/VideoSurfaceGeometry.h\"" in surface_cpp,
            "FFmpegSurface should include the extracted geometry helper")
    require("#include \"render/VideoPixelFormat.h\"" not in surface_cpp and
            "#include \"render/VideoMaterial.h\"" not in surface_cpp,
            "FFmpegSurface should not include pixel/material render internals directly")
    require("struct PixelFormatInfo" not in surface_cpp,
            "FFmpegSurface should not define PixelFormatInfo inline")
    require("enum class PlaneLayout" not in surface_cpp,
            "FFmpegSurface should not define PlaneLayout inline")
    for forbidden in ("class RhiTextureWrapper", "struct PendingUpload",
                      "class VideoMaterial", "class VideoShader"):
        require(forbidden not in surface_cpp,
                f"FFmpegSurface should not define material helper inline: {forbidden}")
    require("class VideoNode" not in surface_cpp,
            "FFmpegSurface should not define VideoNode inline")
    require("QSizeF(video_size).scaled" not in surface_cpp,
            "FFmpegSurface should not calculate aspect-ratio draw rectangles inline")
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
    require("Owns the QSG geometry node used by FFmpegSurface" in node_h,
            "VideoNode.h should explain its file purpose")
    require("Implements FFmpegSurface video node texture lifecycle and frame binding" in node_cpp,
            "VideoNode.cpp should explain its file purpose")
    require("class VideoNode" in node_h and "bool setFrame" in node_h,
            "VideoNode.h should own the node API")
    require("#include \"render/VideoMaterial.h\"" in node_h and
            "PixelFormatInfo" in node_h,
            "VideoNode should depend on material and pixel format details")
    require("setNativeFrame" in node_cpp and "ensureTextures" in node_cpp and "releaseTextures" in node_cpp,
            "VideoNode.cpp should own native/software texture lifecycle")
    require("Calculates the video draw rectangle for FFmpegSurface" in geometry_h,
            "VideoSurfaceGeometry.h should explain its file purpose")
    require("Implements FFmpegSurface aspect-ratio geometry calculations" in geometry_cpp,
            "VideoSurfaceGeometry.cpp should explain its file purpose")
    require("videoDrawRect" in geometry_h and "videoDrawRect" in geometry_cpp,
            "VideoSurfaceGeometry should expose and implement videoDrawRect")
    require("src/render/VideoPixelFormat.h" in cmake and
            "src/render/VideoPixelFormat.cpp" in cmake,
            "PlayPlugin CMake should compile the extracted pixel format helper")
    require("src/render/VideoMaterial.h" in cmake and
            "src/render/VideoMaterial.cpp" in cmake,
            "PlayPlugin CMake should compile the extracted material helper")
    require("src/render/VideoNode.h" in cmake and
            "src/render/VideoNode.cpp" in cmake,
            "PlayPlugin CMake should compile the extracted node helper")
    require("src/render/VideoSurfaceGeometry.h" in cmake and
            "src/render/VideoSurfaceGeometry.cpp" in cmake,
            "PlayPlugin CMake should compile the extracted geometry helper")
    require("playplugin_surface_decoupling_checks" in root_cmake,
            "CTest should run the Surface decoupling check")


if __name__ == "__main__":
    main()
