#include "render/VideoPixelFormat.h"

// Implements FFmpeg pixel format mapping for QRhi texture uploads.
// Keeping this table outside FFmpegSurface makes the format contract reusable by later render backends.

extern "C" {
#include <libavutil/pixfmt.h>
}

int PixelFormatInfo::chromaWidth(int width) const
{
    return (width + chromaWidthDivisor - 1) / chromaWidthDivisor;
}

int PixelFormatInfo::chromaHeight(int height) const
{
    return (height + chromaHeightDivisor - 1) / chromaHeightDivisor;
}

bool PixelFormatInfo::isSemiplanar() const
{
    return planeLayout == PlaneLayout::Semiplanar;
}

PixelFormatInfo PixelFormatInfo::fromAVFormat(int avFormat)
{
    switch (avFormat) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
        return { true, QRhiTexture::R8, QRhiTexture::R8, 2, 2, false, false, 0.0f,
                 PlaneLayout::Planar };

    case AV_PIX_FMT_NV12:
        return { true, QRhiTexture::R8, QRhiTexture::RG8, 2, 2, false, false, 2.0f,
                 PlaneLayout::Semiplanar };

    case AV_PIX_FMT_YUV420P10LE:
        return { true, QRhiTexture::R16, QRhiTexture::R16, 2, 2, true, true, 1.0f,
                 PlaneLayout::Planar };

    case AV_PIX_FMT_P010LE:
        return { true, QRhiTexture::R16, QRhiTexture::RG16, 2, 2, true, false, 3.0f,
                 PlaneLayout::Semiplanar };

    case AV_PIX_FMT_YUV422P10LE:
        return { true, QRhiTexture::R16, QRhiTexture::R16, 2, 1, true, true, 1.0f,
                 PlaneLayout::Planar };

    case AV_PIX_FMT_YUV444P10LE:
        return { true, QRhiTexture::R16, QRhiTexture::R16, 1, 1, true, true, 1.0f,
                 PlaneLayout::Planar };

    default:
        return {};
    }
}

bool PixelFormatInfo::operator==(const PixelFormatInfo& other) const
{
    return lumaFormat == other.lumaFormat
        && chromaFormat == other.chromaFormat
        && chromaWidthDivisor == other.chromaWidthDivisor
        && chromaHeightDivisor == other.chromaHeightDivisor
        && is10bit == other.is10bit
        && needs10BitExpansion == other.needs10BitExpansion
        && formatMode == other.formatMode
        && planeLayout == other.planeLayout;
}

bool PixelFormatInfo::operator!=(const PixelFormatInfo& other) const
{
    return !(*this == other);
}
