#pragma once

// Owns the QSG geometry node used by FFmpegSurface.
// This keeps Scene Graph texture lifecycle and frame binding separate from the QQuickItem facade.

#include "FFmpegUtils.h"
#include "render/VideoMaterial.h"

#if defined(Q_OS_APPLE)
#include "native/AppleMetalVideoTextureBridge.h"
#endif

#include <QSGGeometryNode>
#include <memory>

class QQuickWindow;

class VideoNode : public QSGGeometryNode
{
public:
    VideoNode();
    ~VideoNode() override;

    void setRect(const QRectF& rect);
    bool setFrame(QQuickWindow* window, const VideoFrameDataPtr& frameData);
    bool hasFrame() const;
    QSize videoSize() const;
    void clearToBlack();

private:
    bool setNativeFrame(QQuickWindow* window, const VideoFrameDataPtr& frameData);
    void ensureTextures(QQuickWindow* window, int width, int height, const PixelFormatInfo& format);
    void releaseTextures();

    VideoMaterial m_material;
    bool m_hasFrame = false;
#if defined(Q_OS_APPLE)
    std::unique_ptr<AppleMetalVideoTextureBridge> m_nativeBridge;
#endif
};
