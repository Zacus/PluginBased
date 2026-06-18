#include "AppleMetalVideoTextureBridge.h"

#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

namespace {

struct MetalPlaneFormat
{
    MTLPixelFormat yFormat = MTLPixelFormatInvalid;
    MTLPixelFormat uvFormat = MTLPixelFormatInvalid;
    QRhiTexture::Format yRhiFormat = QRhiTexture::UnknownFormat;
    QRhiTexture::Format uvRhiFormat = QRhiTexture::UnknownFormat;
    bool fullRange = false;
    bool is10bit = false;
};

MetalPlaneFormat mapPixelBufferFormat(OSType format)
{
    switch (format) {
    case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
        return { MTLPixelFormatR8Unorm, MTLPixelFormatRG8Unorm,
                 QRhiTexture::R8, QRhiTexture::RG8, false, false };
    case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
        return { MTLPixelFormatR8Unorm, MTLPixelFormatRG8Unorm,
                 QRhiTexture::R8, QRhiTexture::RG8, true, false };
    case kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange:
        return { MTLPixelFormatR16Unorm, MTLPixelFormatRG16Unorm,
                 QRhiTexture::R16, QRhiTexture::RG16, false, true };
    case kCVPixelFormatType_420YpCbCr10BiPlanarFullRange:
        return { MTLPixelFormatR16Unorm, MTLPixelFormatRG16Unorm,
                 QRhiTexture::R16, QRhiTexture::RG16, true, true };
    default:
        return {};
    }
}

} // namespace

AppleMetalTextureSet::~AppleMetalTextureSet()
{
    if (yCoreVideoTexture)
        CFRelease(static_cast<CVMetalTextureRef>(yCoreVideoTexture));
    if (uvCoreVideoTexture)
        CFRelease(static_cast<CVMetalTextureRef>(uvCoreVideoTexture));
}

AppleMetalVideoTextureBridge::AppleMetalVideoTextureBridge() = default;

AppleMetalVideoTextureBridge::~AppleMetalVideoTextureBridge()
{
    if (m_textureCache)
        CFRelease(static_cast<CVMetalTextureCacheRef>(m_textureCache));
}

bool AppleMetalVideoTextureBridge::isAvailable(QRhi* rhi) const
{
    return rhi && rhi->backend() == QRhi::Metal;
}

std::unique_ptr<AppleMetalTextureSet>
AppleMetalVideoTextureBridge::createTextureSet(QRhi* rhi, const AVFrame* frame)
{
    if (!isAvailable(rhi) || !frame || frame->format != AV_PIX_FMT_VIDEOTOOLBOX)
        return {};

    const auto* handles = static_cast<const QRhiMetalNativeHandles*>(rhi->nativeHandles());
    if (!handles || !handles->dev)
        return {};

    if (!m_textureCache || m_device != handles->dev) {
        if (m_textureCache)
            CFRelease(static_cast<CVMetalTextureCacheRef>(m_textureCache));
        m_textureCache = nullptr;
        m_device = handles->dev;

        id<MTLDevice> metalDevice = reinterpret_cast<id<MTLDevice>>(handles->dev);
        CVMetalTextureCacheRef cache = nullptr;
        const CVReturn ret = CVMetalTextureCacheCreate(kCFAllocatorDefault,
                                                       nullptr,
                                                       metalDevice,
                                                       nullptr,
                                                       &cache);
        if (ret != kCVReturnSuccess)
            return {};
        m_textureCache = cache;
    }

    auto* pixelBuffer = reinterpret_cast<CVPixelBufferRef>(frame->data[3]);
    if (!pixelBuffer || CVPixelBufferGetPlaneCount(pixelBuffer) < 2)
        return {};

    const OSType cvFormat = CVPixelBufferGetPixelFormatType(pixelBuffer);
    const MetalPlaneFormat fmt = mapPixelBufferFormat(cvFormat);
    if (fmt.yFormat == MTLPixelFormatInvalid)
        return {};

    const size_t yWidth = CVPixelBufferGetWidthOfPlane(pixelBuffer, 0);
    const size_t yHeight = CVPixelBufferGetHeightOfPlane(pixelBuffer, 0);
    const size_t uvWidth = CVPixelBufferGetWidthOfPlane(pixelBuffer, 1);
    const size_t uvHeight = CVPixelBufferGetHeightOfPlane(pixelBuffer, 1);
    if (yWidth == 0 || yHeight == 0 || uvWidth == 0 || uvHeight == 0)
        return {};

    CVMetalTextureRef yCvTexture = nullptr;
    CVReturn ret = CVMetalTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault,
        static_cast<CVMetalTextureCacheRef>(m_textureCache),
        pixelBuffer,
        nullptr,
        fmt.yFormat,
        yWidth,
        yHeight,
        0,
        &yCvTexture);
    if (ret != kCVReturnSuccess || !yCvTexture)
        return {};

    CVMetalTextureRef uvCvTexture = nullptr;
    ret = CVMetalTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault,
        static_cast<CVMetalTextureCacheRef>(m_textureCache),
        pixelBuffer,
        nullptr,
        fmt.uvFormat,
        uvWidth,
        uvHeight,
        1,
        &uvCvTexture);
    if (ret != kCVReturnSuccess || !uvCvTexture) {
        CFRelease(yCvTexture);
        return {};
    }

    id<MTLTexture> yMetalTexture = CVMetalTextureGetTexture(yCvTexture);
    id<MTLTexture> uvMetalTexture = CVMetalTextureGetTexture(uvCvTexture);
    if (!yMetalTexture || !uvMetalTexture) {
        CFRelease(yCvTexture);
        CFRelease(uvCvTexture);
        return {};
    }

    auto set = std::make_unique<AppleMetalTextureSet>();
    set->lumaSize = QSize(static_cast<int>(yWidth), static_cast<int>(yHeight));
    set->chromaSize = QSize(static_cast<int>(uvWidth), static_cast<int>(uvHeight));
    set->native.kind = NativeFrameKind::VideoToolbox;
    set->native.pixelFormat = static_cast<int>(cvFormat);
    set->native.fullRange = fmt.fullRange;
    set->native.is10bit = fmt.is10bit;
    set->native.bt709 =
        frame->colorspace == AVCOL_SPC_BT709 ||
        (frame->colorspace == AVCOL_SPC_UNSPECIFIED && frame->width >= 1280);

    set->yTexture.reset(rhi->newTexture(fmt.yRhiFormat, set->lumaSize));
    set->uvTexture.reset(rhi->newTexture(fmt.uvRhiFormat, set->chromaSize));
    set->vPlaceholderTexture.reset(rhi->newTexture(QRhiTexture::R8, QSize(1, 1)));
    if (!set->yTexture || !set->uvTexture || !set->vPlaceholderTexture) {
        CFRelease(yCvTexture);
        CFRelease(uvCvTexture);
        return {};
    }

    QRhiTexture::NativeTexture yNative = { reinterpret_cast<quint64>(yMetalTexture), 0 };
    QRhiTexture::NativeTexture uvNative = { reinterpret_cast<quint64>(uvMetalTexture), 0 };
    if (!set->yTexture->createFrom(yNative) ||
        !set->uvTexture->createFrom(uvNative) ||
        !set->vPlaceholderTexture->create()) {
        CFRelease(yCvTexture);
        CFRelease(uvCvTexture);
        return {};
    }

    set->yCoreVideoTexture = yCvTexture;
    set->uvCoreVideoTexture = uvCvTexture;
    return set;
}
