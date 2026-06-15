#include "FFmpegSurface.h"
#include "render/VideoMaterial.h"
#include "render/VideoPixelFormat.h"

#if defined(Q_OS_APPLE)
#include "native/AppleMetalVideoTextureBridge.h"
#endif

#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGRendererInterface>
#include <QMutexLocker>

#include <rhi/qrhi.h>

#include <memory>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

// ══════════════════════════════════════════════════════════════════════════════
// VideoNode
// ══════════════════════════════════════════════════════════════════════════════

class VideoNode : public QSGGeometryNode
{
public:
    VideoNode()
    {
        setFlag(OwnsGeometry);
        // 【不设 OwnsMaterial】：m_material_ 是成员变量，不能被 Qt delete

        auto* g = new QSGGeometry(
            QSGGeometry::defaultAttributes_TexturedPoint2D(), 4);
        g->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        setGeometry(g);
        setMaterial(&m_material_);
    }

    ~VideoNode() {
        releaseTextures();
    }

    // 更新顶点几何（由 FFmpegSurface 根据 aspectRatioMode 计算后传入）
    void setRect(const QRectF& rect)
    {
        QSGGeometry::updateTexturedRectGeometry(
            geometry(), rect, QRectF(0, 0, 1, 1));
        markDirty(QSGNode::DirtyGeometry);
    }

    bool setNativeFrame(QQuickWindow* window, const VideoFrameDataPtr& frameData)
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
        m_material_.tex_y = textures->yTexture.get();
        m_material_.tex_u = textures->uvTexture.get();
        m_material_.tex_v = textures->vPlaceholderTexture.get();
        m_material_.size = textures->lumaSize;
        m_material_.fmtInfo = PixelFormatInfo::fromAVFormat(textures->native.is10bit
            ? AV_PIX_FMT_P010LE
            : AV_PIX_FMT_NV12);
        m_material_.fullRange = textures->native.fullRange;
        m_material_.bt709 = textures->native.bt709;
        m_material_.cachedFullRange = textures->native.fullRange;
        m_material_.cachedBt709 = textures->native.bt709;
        m_material_.paramsDirty = true;
        m_material_.pendingBlackFrame = false;
        m_material_.pending.valid = false;
        m_material_.pending.frameData.reset();
        m_material_.currentFrame = frameData;
        m_material_.nativeTextures = std::move(textures);
        m_material_.usingNativeTextures = true;
        m_hasFrame = true;
        markDirty(QSGNode::DirtyMaterial);
        return true;
#else
        Q_UNUSED(window);
        Q_UNUSED(frameData);
        return false;
#endif
    }

    // 拷贝帧数据；GPU 上传延迟到 VideoShader 回调中执行
    bool setFrame(QQuickWindow* window, const VideoFrameDataPtr& frameData)
    {
        if (!window || !frameData)
            return false;
        const AVFrame* frame = frameData->frame.get();
        if (!frame)
            return false;

        if (frameData->native.kind == NativeFrameKind::VideoToolbox)
            return setNativeFrame(window, frameData);

        const PixelFormatInfo fmt = PixelFormatInfo::fromAVFormat(frame->format);
        if (!fmt.valid) {
            qWarning("FFmpegSurface: unsupported pixel format %d, frame dropped.", frame->format);
            return false;
        }

        ensureTextures(window, frame->width, frame->height, fmt);

        // ✅ 只存 shared_ptr，零拷贝
        m_material_.pending.frameData = frameData;
        m_material_.pending.valid     = true;
        m_hasFrame = true;

        // 色彩空间参数（仅变化时才标脏）
        if (m_material_.cachedFullRange != frameData->fullRange ||
            m_material_.cachedBt709     != frameData->bt709)
        {
            m_material_.fullRange       = frameData->fullRange;
            m_material_.bt709           = frameData->bt709;
            m_material_.cachedFullRange = frameData->fullRange;
            m_material_.cachedBt709     = frameData->bt709;
            m_material_.paramsDirty     = true;
        }

        markDirty(QSGNode::DirtyMaterial);
        return true;
    }

    bool hasFrame() const { return m_hasFrame; }

    // 用于 FFmpegSurface 在无新帧时仍能计算 drawRect
    QSize videoSize() const { return m_material_.size; }

    void clearToBlack() {
        m_hasFrame = false;
        if (m_material_.usingNativeTextures) {
            releaseTextures();
            markDirty(QSGNode::DirtyMaterial);
            return;
        }
        if (m_material_.tex_y) {  // 纹理存在才填，否则直接透明
            m_material_.pendingBlackFrame = true;
            m_material_.paramsDirty = true;
            markDirty(QSGNode::DirtyMaterial);
        }
    }

private:
    void ensureTextures(QQuickWindow* window, int w, int h,  const PixelFormatInfo& fmt)
    {
        if (m_material_.size == QSize(w, h) &&
            m_material_.tex_y &&
            m_material_.fmtInfo == fmt)
            return;

        releaseTextures();

        const int cw = fmt.chromaWidth (w);
        const int ch = fmt.chromaHeight(h);

        QRhi* rhi = window->rhi();
        m_material_.tex_y = rhi->newTexture(fmt.lumaFormat, { w,  h  });
        m_material_.tex_y->create();
        m_material_.tex_u = rhi->newTexture(fmt.chromaFormat, { cw, ch });
        m_material_.tex_u->create();
        const QSize vSize = fmt.isSemiplanar() ? QSize(1, 1) : QSize(cw, ch);
        const QRhiTexture::Format vFormat = fmt.isSemiplanar() ? QRhiTexture::R8 : fmt.chromaFormat;
        m_material_.tex_v = rhi->newTexture(vFormat, vSize);
        m_material_.tex_v->create();
        m_material_.size    = { w, h };
        m_material_.fmtInfo = fmt;

        // ✅ 填充占位数据：Y=16, U=128, V=128 → 纯黑（limited range）
        m_material_.pendingBlackFrame = true; // ← 只立 flag，不再填 QByteArray
        m_material_.paramsDirty     = true;
        m_material_.cachedFullRange = !m_material_.fullRange;  // 故意与当前值不同，触发下一帧重建
        m_material_.cachedBt709     = !m_material_.bt709;
    }

    void releaseTextures()
    {
        if (m_material_.usingNativeTextures) {
#if defined(Q_OS_APPLE)
            m_material_.nativeTextures.reset();
#endif
        } else {
            delete m_material_.tex_y;
            delete m_material_.tex_u;
            delete m_material_.tex_v;
        }
        m_material_.tex_y = nullptr;
        m_material_.tex_u = nullptr;
        m_material_.tex_v = nullptr;
        m_material_.usingNativeTextures = false;
        m_material_.pending.valid = false;
        m_material_.pending.frameData.reset();
        m_material_.pendingBlackFrame = false;
        m_material_.size = {};
        m_material_.currentFrame.reset(); 
    }

    
    VideoMaterial m_material_;
    bool m_hasFrame  = false;
#if defined(Q_OS_APPLE)
    std::unique_ptr<AppleMetalVideoTextureBridge> m_nativeBridge;
#endif
};

// ══════════════════════════════════════════════════════════════════════════════
// FFmpegSurface
// ══════════════════════════════════════════════════════════════════════════════

FFmpegSurface::FFmpegSurface(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

// FFmpegSurface::~FFmpegSurface() = default;
// m_pendingFrame 是 shared_ptr，析构时自动减引用计数

void FFmpegSurface::setAspectRatioMode(Qt::AspectRatioMode mode)
{
    if (m_aspectRatioMode == mode)
        return;
    m_aspectRatioMode = mode;
    emit aspectRatioModeChanged();
    update();
}

bool FFmpegSurface::supportsNativeVideoToolboxRendering() const
{
#if defined(Q_OS_APPLE)
    return window() &&
           window()->rendererInterface() &&
           window()->rendererInterface()->graphicsApi() == QSGRendererInterface::MetalRhi;
#else
    return false;
#endif
}

// 可从任意线程调用（VideoRenderer::frameReady 可能跨线程发出）
void FFmpegSurface::onFrameReady(const VideoFrameDataPtr& frame)
{
    {
        QMutexLocker lock(&m_mutex);
        m_pendingFrame = frame;   // shared_ptr 赋值，旧帧引用计数 -1，线程安全
        m_dirty        = true;
    }
    // update() 必须在 GUI 线程执行；QueuedConnection 保证正确派发
    QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
}

QSGNode* FFmpegSurface::updatePaintNode(QSGNode*         old_node,
                                        UpdatePaintNodeData*)
{
    auto* node = static_cast<VideoNode*>(old_node);

    bool clearPending = false;
    // ── 取出待上屏的帧（move 转移所有权，避免引用计数抖动）────────────────
    VideoFrameDataPtr frame;
    {
        QMutexLocker lock(&m_mutex);
        clearPending   = m_clearPending;
        m_clearPending = false;
        if (m_dirty) {
            frame   = std::move(m_pendingFrame);
            m_dirty = false;
        }
    }

    // 清屏：不删节点，只填黑，GPU 纹理安全
    if (clearPending && node) {
        node->clearToBlack();
    }

    if (!frame && (!node || !node->hasFrame())) {
        delete node;
        return nullptr;
    }

    // ── 计算绘制区域（支持 KeepAspectRatio / KeepAspectRatioByExpanding）──
    QRectF draw_rect = boundingRect();

    const QSize video_size = frame
        ? QSize(frame->frame ? frame->frame->width  : 0,
                frame->frame ? frame->frame->height : 0)
        : (node ? node->videoSize() : QSize{});

    if (video_size.isValid() && m_aspectRatioMode != Qt::IgnoreAspectRatio) {
        const QSizeF scaled =
            QSizeF(video_size).scaled(boundingRect().size(), m_aspectRatioMode);
        draw_rect = QRectF(
            (boundingRect().width()  - scaled.width())  / 2.0,
            (boundingRect().height() - scaled.height()) / 2.0,
            scaled.width(),
            scaled.height());
    }

    // ── 创建或更新节点 ────────────────────────────────────────────────────
    if (!node)
        node = new VideoNode();

    node->setRect(draw_rect);

    if (frame && !node->setFrame(window(), frame) &&
        frame->native.kind == NativeFrameKind::VideoToolbox) {
        if (m_nativeRenderingFailureLogs < 3) {
            qWarning("FFmpegSurface: native VideoToolbox texture creation failed, "
                     "disabling native rendering for future frames.");
            ++m_nativeRenderingFailureLogs;
        }
        emit nativeRenderingFailed();
    }

    return node;
}

void FFmpegSurface::clear()
{
    {
        QMutexLocker lock(&m_mutex);
        m_clearPending = true; // 标记正在清除，updatePaintNode 里可选地处理（如显示纯黑）
    }
    QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
}
