#include "video/FFmpegSurface.h"
#include "video/render/VideoNode.h"
#include "video/render/VideoSurfaceGeometry.h"

#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QMutexLocker>
#include <cstdlib>

namespace {

std::uint64_t cpuFrameBytes(const AVFrame* frame)
{
    if (!frame)
        return 0;

    const int packedSize = av_image_get_buffer_size(
        static_cast<AVPixelFormat>(frame->format),
        frame->width,
        frame->height,
        1);
    if (packedSize > 0)
        return static_cast<std::uint64_t>(packedSize);

    std::uint64_t total = 0;
    for (int plane = 0; plane < AV_NUM_DATA_POINTERS; ++plane) {
        if (!frame->data[plane] || frame->linesize[plane] == 0)
            continue;
        total += static_cast<std::uint64_t>(std::abs(frame->linesize[plane]))
            * static_cast<std::uint64_t>(frame->height);
    }
    return total;
}

} // namespace

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

FFmpegSurfaceDiagnostics FFmpegSurface::diagnosticsSnapshot() const
{
    return {
        .nativeTextureCreated = m_nativeTextureCreated.load(std::memory_order_relaxed),
        .nativeTextureDrawn = m_nativeTextureDrawn.load(std::memory_order_relaxed),
        .cpuTransferred = m_cpuTransferred.load(std::memory_order_relaxed),
        .cpuMemcpy = m_cpuMemcpy.load(std::memory_order_relaxed),
    };
}

void FFmpegSurface::resetDiagnostics()
{
    m_nativeTextureCreated.store(0, std::memory_order_relaxed);
    m_nativeTextureDrawn.store(0, std::memory_order_relaxed);
    m_cpuTransferred.store(0, std::memory_order_relaxed);
    m_cpuMemcpy.store(0, std::memory_order_relaxed);
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

    if (frame) {
        const bool nativeFrame = frame->native.kind == NativeFrameKind::VideoToolbox;
        const bool accepted = node->setFrame(window(), frame);
        if (!accepted && nativeFrame) {
            if (m_nativeRenderingFailureLogs < 3) {
                qWarning("FFmpegSurface: native VideoToolbox texture creation failed, "
                         "disabling native rendering for future frames.");
                ++m_nativeRenderingFailureLogs;
            }
            emit nativeRenderingFailed();
        } else if (accepted && nativeFrame) {
            m_nativeTextureCreated.fetch_add(1, std::memory_order_relaxed);
            m_nativeTextureDrawn.fetch_add(1, std::memory_order_relaxed);
        } else if (accepted) {
            m_cpuTransferred.fetch_add(1, std::memory_order_relaxed);
            m_cpuMemcpy.fetch_add(cpuFrameBytes(frame->frame.get()), std::memory_order_relaxed);
        }
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
