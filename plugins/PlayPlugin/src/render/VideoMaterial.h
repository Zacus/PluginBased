#pragma once

// Owns the QSG material state used by FFmpegSurface video nodes.
// VideoNode keeps one VideoMaterial instance while the shader implementation lives in VideoMaterial.cpp.

#include "FFmpegUtils.h"
#include "render/VideoPixelFormat.h"

#if defined(Q_OS_APPLE)
#include "native/AppleMetalVideoTextureBridge.h"
#endif

#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGRendererInterface>
#include <QSize>
#include <memory>
#include <rhi/qrhi.h>

struct PendingUpload
{
    VideoFrameDataPtr frameData;
    bool valid = false;
};

class VideoMaterial : public QSGMaterial
{
public:
    QRhiTexture* tex_y = nullptr;
    QRhiTexture* tex_u = nullptr;
    QRhiTexture* tex_v = nullptr;
    QSize size;

    PendingUpload pending;
    PixelFormatInfo fmtInfo;

    float opacity = 1.0f;
    bool fullRange = false;
    bool bt709 = false;
    bool pendingBlackFrame = false;

    bool cachedFullRange = false;
    bool cachedBt709 = false;
    bool paramsDirty = true;
    VideoFrameDataPtr currentFrame;
#if defined(Q_OS_APPLE)
    std::unique_ptr<AppleMetalTextureSet> nativeTextures;
#endif
    bool usingNativeTextures = false;

    QSGMaterialType* type() const override;
    int compare(const QSGMaterial* other) const override;
    QSGMaterialShader* createShader(QSGRendererInterface::RenderMode) const override;
};
