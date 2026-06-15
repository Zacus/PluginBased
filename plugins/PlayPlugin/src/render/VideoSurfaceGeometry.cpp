#include "render/VideoSurfaceGeometry.h"

// Implements FFmpegSurface aspect-ratio geometry calculations.
// The helper is intentionally independent of Scene Graph nodes and frame ownership.

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
