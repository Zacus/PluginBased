#pragma once

// 描述 FFmpegSurface 渲染所需的 YUV/半平面视频格式。
// Surface 和后续渲染后端通过它把 FFmpeg 像素格式映射到 QRhi 纹理布局。

#include <QSize>
#include <rhi/qrhi.h>

enum class PlaneLayout { Planar, Semiplanar };

struct PixelFormatInfo
{
    bool valid = false;
    QRhiTexture::Format lumaFormat = QRhiTexture::R8;
    QRhiTexture::Format chromaFormat = QRhiTexture::R8;
    int chromaWidthDivisor = 2;
    int chromaHeightDivisor = 2;
    bool is10bit = false;
    bool needs10BitExpansion = false;
    float formatMode = 0.0f;
    PlaneLayout planeLayout = PlaneLayout::Planar;

    int chromaWidth(int width) const;
    int chromaHeight(int height) const;
    bool isSemiplanar() const;

    static PixelFormatInfo fromAVFormat(int avFormat);

    bool operator==(const PixelFormatInfo& other) const;
    bool operator!=(const PixelFormatInfo& other) const;
};
