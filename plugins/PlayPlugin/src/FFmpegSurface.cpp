#include "FFmpegSurface.h"
#include "render/VideoNode.h"
#include "render/VideoSurfaceGeometry.h"

#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QMutexLocker>

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

    const QSize video_size = frame
        ? QSize(frame->frame ? frame->frame->width  : 0,
                frame->frame ? frame->frame->height : 0)
        : (node ? node->videoSize() : QSize{});
    const QRectF draw_rect =
        VideoSurfaceGeometry::videoDrawRect(boundingRect(), video_size, m_aspectRatioMode);

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
