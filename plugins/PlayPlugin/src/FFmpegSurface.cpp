#include "FFmpegSurface.h"

#include <QSGTexture>
#include <QQuickWindow>

FFmpegSurface::FFmpegSurface(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true); // 告知 Qt 这个 Item 有自定义渲染内容
}

void FFmpegSurface::setAspectRatioMode(Qt::AspectRatioMode mode)
{
    if (m_aspectRatioMode == mode) return;
    m_aspectRatioMode = mode;
    emit aspectRatioModeChanged();
    update();
}

// ── 主线程：接收新帧 ──────────────────────────────────────────────────────────
void FFmpegSurface::onFrameReady(const QImage& image)
{
    {
        QMutexLocker lk(&m_mutex);
        m_pendingImage = image;
        m_dirty        = true;
    }
    // 触发 Qt 场景图更新，会在下一个 vsync 调用 updatePaintNode
    update();
}

// ── Qt 渲染线程：更新场景图节点 ──────────────────────────────────────────────
QSGNode* FFmpegSurface::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    QImage image;
    {
        QMutexLocker lk(&m_mutex);
        if (!m_dirty) return oldNode; // 没有新帧，保持原样
        image   = m_pendingImage;
        m_dirty = false;
    }

    if (image.isNull()) return oldNode;

    // 确保图像格式是 ARGB32（与 swscale 输出的 AV_PIX_FMT_RGB32 对应）
    if (image.format() != QImage::Format_ARGB32)
        image = image.convertToFormat(QImage::Format_ARGB32);

    // 创建或复用 QSGImageNode
    QSGImageNode* node = static_cast<QSGImageNode*>(oldNode);
    if (!node) {
        node = window()->createImageNode();
        node->setOwnsTexture(true); // node 负责销毁 texture
    }

    // 用 QImage 创建 GPU 纹理（上传到 GPU，之后 CPU 侧的 QImage 可以释放）
    QSGTexture* texture = window()->createTextureFromImage(
        image, QQuickWindow::TextureIsOpaque);
    node->setTexture(texture);

    // 计算目标矩形（保持宽高比）
    const QSizeF itemSize(width(), height());
    const QSizeF imgSize = image.size();

    QRectF targetRect;
    if (m_aspectRatioMode == Qt::IgnoreAspectRatio) {
        targetRect = QRectF(QPointF(0, 0), itemSize);
    } else {
        const QSizeF scaled = imgSize.scaled(itemSize, m_aspectRatioMode);
        const qreal x = (itemSize.width()  - scaled.width())  / 2.0;
        const qreal y = (itemSize.height() - scaled.height()) / 2.0;
        targetRect = QRectF(QPointF(x, y), scaled);
    }

    node->setRect(targetRect);
    node->setFiltering(QSGTexture::Linear); // 双线性插值，缩放时更平滑
    return node;
}
