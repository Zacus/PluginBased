#pragma once

// 计算 FFmpegSurface 中视频实际绘制矩形。
// 将纯计算逻辑放在 QQuickItem 外部，便于测试和复用宽高比行为。

#include <QRectF>
#include <QSize>
#include <Qt>

namespace VideoSurfaceGeometry {

QRectF videoDrawRect(const QRectF& bounds,
                     const QSize& videoSize,
                     Qt::AspectRatioMode aspectRatioMode);

}
