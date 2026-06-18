#include "video/render/VideoNode.h"

// 实现 FFmpegSurface 视频节点的纹理生命周期和帧绑定。
// 该节点持有软件上传用 QRhi 纹理；平台支持时也持有原生纹理集合。

#if defined(Q_OS_APPLE)
#include "video/native/AppleMetalVideoTextureBridge.h"
#endif

#include <QQuickWindow>
#include <QSGGeometry>
#include <QSGNode>
#include <QDebug>
#include <rhi/qrhi.h>

extern "C" {
#include <libavutil/pixfmt.h>
}

VideoNode::VideoNode()
{
    setFlag(OwnsGeometry);

    auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4);
    geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
    setGeometry(geometry);
    setMaterial(&m_material);
}

VideoNode::~VideoNode()
{
    releaseTextures();
}

void VideoNode::setRect(const QRectF& rect)
{
    QSGGeometry::updateTexturedRectGeometry(geometry(), rect, QRectF(0, 0, 1, 1));
    markDirty(QSGNode::DirtyGeometry);
}

bool VideoNode::setNativeFrame(QQuickWindow* window, const VideoFrameDataPtr& frameData)
{
#if defined(Q_OS_APPLE)
    if (!window || !frameData || !frameData->frame ||
        frameData->native.kind != NativeFrameKind::VideoToolbox)
        return false;

    if (!m_nativeBridge)
        m_nativeBridge = std::make_unique<AppleMetalVideoTextureBridge>();
    if (!m_nativeBridge->isAvailable(window->rhi()))
        return false;

    auto textures = m_nativeBridge->createTextureSet(window->rhi(), frameData->frame.get());
    if (!textures)
        return false;

    releaseTextures();
    m_material.tex_y = textures->yTexture.get();
    m_material.tex_u = textures->uvTexture.get();
    m_material.tex_v = textures->vPlaceholderTexture.get();
    m_material.size = textures->lumaSize;
    m_material.fmtInfo = PixelFormatInfo::fromAVFormat(textures->native.is10bit
        ? AV_PIX_FMT_P010LE
        : AV_PIX_FMT_NV12);
    m_material.fullRange = textures->native.fullRange;
    m_material.bt709 = textures->native.bt709;
    m_material.cachedFullRange = textures->native.fullRange;
    m_material.cachedBt709 = textures->native.bt709;
    m_material.paramsDirty = true;
    m_material.pendingBlackFrame = false;
    m_material.pending.valid = false;
    m_material.pending.frameData.reset();
    m_material.currentFrame = frameData;
    m_material.nativeTextures = std::move(textures);
    m_material.usingNativeTextures = true;
    m_hasFrame = true;
    markDirty(QSGNode::DirtyMaterial);
    return true;
#else
    Q_UNUSED(window);
    Q_UNUSED(frameData);
    return false;
#endif
}

bool VideoNode::setFrame(QQuickWindow* window, const VideoFrameDataPtr& frameData)
{
    if (!window || !frameData)
        return false;
    const AVFrame* frame = frameData->frame.get();
    if (!frame)
        return false;

    if (frameData->native.kind == NativeFrameKind::VideoToolbox)
        return setNativeFrame(window, frameData);

    const PixelFormatInfo format = PixelFormatInfo::fromAVFormat(frame->format);
    if (!format.valid) {
        qWarning("FFmpegSurface: unsupported pixel format %d, frame dropped.", frame->format);
        return false;
    }

    ensureTextures(window, frame->width, frame->height, format);

    m_material.pending.frameData = frameData;
    m_material.pending.valid = true;
    m_hasFrame = true;

    if (m_material.cachedFullRange != frameData->fullRange ||
        m_material.cachedBt709 != frameData->bt709) {
        m_material.fullRange = frameData->fullRange;
        m_material.bt709 = frameData->bt709;
        m_material.cachedFullRange = frameData->fullRange;
        m_material.cachedBt709 = frameData->bt709;
        m_material.paramsDirty = true;
    }

    markDirty(QSGNode::DirtyMaterial);
    return true;
}

bool VideoNode::hasFrame() const
{
    return m_hasFrame;
}

QSize VideoNode::videoSize() const
{
    return m_material.size;
}

void VideoNode::clearToBlack()
{
    m_hasFrame = false;
    if (m_material.usingNativeTextures) {
        releaseTextures();
        markDirty(QSGNode::DirtyMaterial);
        return;
    }
    if (m_material.tex_y) {
        m_material.pendingBlackFrame = true;
        m_material.paramsDirty = true;
        markDirty(QSGNode::DirtyMaterial);
    }
}

void VideoNode::ensureTextures(QQuickWindow* window,
                               int width,
                               int height,
                               const PixelFormatInfo& format)
{
    if (m_material.size == QSize(width, height) &&
        m_material.tex_y &&
        m_material.fmtInfo == format)
        return;

    releaseTextures();

    const int chromaWidth = format.chromaWidth(width);
    const int chromaHeight = format.chromaHeight(height);

    QRhi* rhi = window->rhi();
    m_material.tex_y = rhi->newTexture(format.lumaFormat, { width, height });
    m_material.tex_y->create();
    m_material.tex_u = rhi->newTexture(format.chromaFormat, { chromaWidth, chromaHeight });
    m_material.tex_u->create();
    const QSize vSize = format.isSemiplanar() ? QSize(1, 1) : QSize(chromaWidth, chromaHeight);
    const QRhiTexture::Format vFormat =
        format.isSemiplanar() ? QRhiTexture::R8 : format.chromaFormat;
    m_material.tex_v = rhi->newTexture(vFormat, vSize);
    m_material.tex_v->create();
    m_material.size = { width, height };
    m_material.fmtInfo = format;

    m_material.pendingBlackFrame = true;
    m_material.paramsDirty = true;
    m_material.cachedFullRange = !m_material.fullRange;
    m_material.cachedBt709 = !m_material.bt709;
}

void VideoNode::releaseTextures()
{
    if (m_material.usingNativeTextures) {
#if defined(Q_OS_APPLE)
        m_material.nativeTextures.reset();
#endif
    } else {
        delete m_material.tex_y;
        delete m_material.tex_u;
        delete m_material.tex_v;
    }
    m_material.tex_y = nullptr;
    m_material.tex_u = nullptr;
    m_material.tex_v = nullptr;
    m_material.usingNativeTextures = false;
    m_material.pending.valid = false;
    m_material.pending.frameData.reset();
    m_material.pendingBlackFrame = false;
    m_material.size = {};
    m_material.currentFrame.reset();
}
