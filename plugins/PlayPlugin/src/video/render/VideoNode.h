#pragma once

// 持有 FFmpegSurface 使用的 QSG 几何节点。
// Scene Graph 纹理生命周期和帧绑定逻辑放在这里，避免混入 QQuickItem 门面。

#include "common/FFmpegUtils.h"
#include "video/render/VideoMaterial.h"

#if defined(Q_OS_APPLE)
#include "video/native/AppleMetalVideoTextureBridge.h"
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
