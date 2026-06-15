#pragma once

// Tracks throttled FFmpeg decoder performance counters.
// FFmpegDecoder owns one logger on its decode thread and updates these counters while processing frames.

#include <QElapsedTimer>
#include <QString>
#include <QtGlobal>

extern "C" {
#include <libavutil/pixfmt.h>
}

struct DecodePerformanceStats
{
    qint64 decodedVideoFrames = 0;
    qint64 hardwareVideoFrames = 0;
    qint64 transferredVideoFrames = 0;
    qint64 transferFailures = 0;
    qint64 transferTotalUs = 0;
    qint64 transferMaxUs = 0;
    qint64 nativeVideoFrames = 0;
    qint64 nativeFallbackVideoFrames = 0;
    qint64 normalizedVideoFrames = 0;
    qint64 normalizeTotalUs = 0;
    qint64 normalizeMaxUs = 0;
    qint64 queuedVideoFrames = 0;
    qint64 queueDroppedVideoFrames = 0;
    int sourcePixelFormat = AV_PIX_FMT_NONE;
    int cpuPixelFormat = AV_PIX_FMT_NONE;
};

class DecodePerformanceLogger
{
public:
    DecodePerformanceStats& stats() { return m_stats; }
    const DecodePerformanceStats& stats() const { return m_stats; }

    void reset();
    void maybeLog(const QString& activeVideoDecoderName);

private:
    DecodePerformanceStats m_stats;
    QElapsedTimer m_logTimer;
};
