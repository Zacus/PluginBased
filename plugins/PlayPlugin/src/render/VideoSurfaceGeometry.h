#pragma once

// Calculates the video draw rectangle for FFmpegSurface.
// Keeping this pure helper outside the QQuickItem makes aspect-ratio behavior easier to test and reuse.

#include <QRectF>
#include <QSize>
#include <Qt>

namespace VideoSurfaceGeometry {

QRectF videoDrawRect(const QRectF& bounds,
                     const QSize& videoSize,
                     Qt::AspectRatioMode aspectRatioMode);

}
