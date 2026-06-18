#pragma once

// 持有 FFmpegSurface 视频节点使用的 QSG 材质状态。
// VideoNode 保存一个 VideoMaterial 实例，具体 shader 实现在 VideoMaterial.cpp 中。

#include "common/FFmpegUtils.h"
#include "video/render/VideoPixelFormat.h"

#if defined(Q_OS_APPLE)
#include "video/native/AppleMetalVideoTextureBridge.h"
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
