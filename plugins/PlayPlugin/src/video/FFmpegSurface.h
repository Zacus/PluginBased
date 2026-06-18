/*
 * @Author: zs
 * @Date: 2026-04-07 15:41:46
 * @LastEditors: zs
 * @LastEditTime: 2026-05-07 16:35:19
 * @FilePath: /PluginBased/plugins/PlayPlugin/src/video/FFmpegSurface.h
 * @Description:
 *
 * Copyright (c) 2026 by zs, All Rights Reserved.
 */
#pragma once

#include "common/FFmpegUtils.h"

#include <QMutex>
#include <QQmlEngine>
#include <QQuickItem>

/**
 * @brief FFmpegSurface — QML 视频渲染组件
 *
 * 继承 QQuickItem，重写 updatePaintNode，在 Scene Graph 中把 YUV plane
 * 作为纹理交给 shader 采样上屏，避免 CPU 侧 swscale + QImage 转换。
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
 *   updatePaintNode 在 Qt 渲染线程调用，两者通过 m_mutex + m_pendingFrame 同步。
 */
class FFmpegSurface : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Qt::AspectRatioMode aspectRatioMode READ aspectRatioMode WRITE setAspectRatioMode
                   NOTIFY aspectRatioModeChanged)

  public:
    explicit FFmpegSurface(QQuickItem* parent = nullptr);

    Qt::AspectRatioMode aspectRatioMode() const
    {
        return m_aspectRatioMode;
    }
    void setAspectRatioMode(Qt::AspectRatioMode mode);
    Q_INVOKABLE bool supportsNativeVideoToolboxRendering() const;

  public slots:
    /** 接收 VideoRenderer::frameReady 信号 */
    void onFrameReady(const VideoFrameDataPtr& frame);
    void clear();

  signals:
    void aspectRatioModeChanged();
    void nativeRenderingFailed();

  protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;

  private:
    QMutex m_mutex;
    VideoFrameDataPtr m_pendingFrame; // 待上屏的帧（主线程写，渲染线程读）
    bool m_dirty = false;             // 是否有新帧待渲染
    bool m_clearPending = false;
    int m_nativeRenderingFailureLogs = 0;

    Qt::AspectRatioMode m_aspectRatioMode = Qt::KeepAspectRatio;
};
