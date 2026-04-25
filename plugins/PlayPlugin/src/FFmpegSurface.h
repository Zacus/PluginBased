#pragma once

#include <QQuickItem>
#include <QImage>
#include <QSGImageNode>
#include <QMutex>
#include <QQmlEngine>

/**
 * @brief FFmpegSurface — QML 视频渲染组件
 *
 * 继承 QQuickItem，重写 updatePaintNode，用 QSGImageNode 把 QImage
 * 上屏。QSGImageNode 在 Qt 渲染线程执行，零主线程 CPU 消耗。
 *
 * 使用方式（QML）：
 *   FFmpegSurface {
 *       id: videoSurface
 *       anchors.fill: parent
 *   }
 *   // C++ 侧：
 *   connect(videoRenderer, &VideoRenderer::frameReady,
 *           surface, &FFmpegSurface::onFrameReady);
 *
 * 线程安全：
 *   onFrameReady 在主线程被调用（VideoRenderer 运行在主线程），
 *   updatePaintNode 在 Qt 渲染线程调用，两者通过 m_mutex + m_pendingImage 同步。
 */
class FFmpegSurface : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Qt::AspectRatioMode aspectRatioMode
               READ aspectRatioMode WRITE setAspectRatioMode
               NOTIFY aspectRatioModeChanged)

public:
    explicit FFmpegSurface(QQuickItem* parent = nullptr);

    Qt::AspectRatioMode aspectRatioMode() const { return m_aspectRatioMode; }
    void setAspectRatioMode(Qt::AspectRatioMode mode);

public slots:
    /** 接收 VideoRenderer::frameReady 信号 */
    void onFrameReady(const QImage& image);

signals:
    void aspectRatioModeChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;

private:
    QMutex  m_mutex;
    QImage  m_pendingImage;   // 待上屏的帧（主线程写，渲染线程读）
    bool    m_dirty = false;  // 是否有新帧待渲染

    Qt::AspectRatioMode m_aspectRatioMode = Qt::KeepAspectRatio;
};
