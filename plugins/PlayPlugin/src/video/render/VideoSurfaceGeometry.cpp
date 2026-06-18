#include "video/render/VideoSurfaceGeometry.h"

// 实现 FFmpegSurface 的宽高比几何计算。
// 该辅助逻辑刻意不依赖 Scene Graph 节点或视频帧所有权。

#include <QSizeF>

namespace VideoSurfaceGeometry {

QRectF videoDrawRect(const QRectF& bounds,
                     const QSize& videoSize,
                     Qt::AspectRatioMode aspectRatioMode)
{
    if (!videoSize.isValid() || aspectRatioMode == Qt::IgnoreAspectRatio)
        return bounds;

    const QSizeF scaled = QSizeF(videoSize).scaled(bounds.size(), aspectRatioMode);
    return QRectF((bounds.width() - scaled.width()) / 2.0,
                  (bounds.height() - scaled.height()) / 2.0,
                  scaled.width(),
                  scaled.height());
}

}
