#include "decode/DecodePerformance.h"

// 实现带节流的解码性能日志输出。
// 日志格式和输出间隔状态集中在这里，避免混入 FFmpegDecoder 的包/帧控制流。

#include "Logger.h"

#include <string>

extern "C" {
#include <libavutil/pixdesc.h>
}

namespace {

constexpr int PerformanceLogIntervalMs = 2000;

std::string pixelFormatName(int format)
{
    if (format == AV_PIX_FMT_NONE)
        return "none";
    const char* name = av_get_pix_fmt_name(static_cast<AVPixelFormat>(format));
    return name ? name : "unknown";
}

qint64 averageUs(qint64 totalUs, qint64 count)
{
    return count > 0 ? totalUs / count : 0;
}

} // namespace

void DecodePerformanceLogger::reset()
{
    m_stats = {};
    m_logTimer.restart();
}

void DecodePerformanceLogger::maybeLog(const QString& activeVideoDecoderName)
{
    if (!m_logTimer.isValid())
        m_logTimer.start();
    if (m_logTimer.elapsed() < PerformanceLogIntervalMs)
        return;
    if (m_stats.decodedVideoFrames <= 0)
    {
        m_logTimer.restart();
        return;
    }

    LOG_INFO("PlayPerf: decoder backend={} decoded={} hw={} native={} native_fallback={} "
             "transfer={} fail={} "
             "transfer_avg_us={} transfer_max_us={} normalize={} normalize_avg_us={} "
             "normalize_max_us={} queued={} queue_drop={} src_fmt={} cpu_fmt={}",
             activeVideoDecoderName.toStdString(),
             m_stats.decodedVideoFrames,
             m_stats.hardwareVideoFrames,
             m_stats.nativeVideoFrames,
             m_stats.nativeFallbackVideoFrames,
             m_stats.transferredVideoFrames,
             m_stats.transferFailures,
             averageUs(m_stats.transferTotalUs, m_stats.transferredVideoFrames),
             m_stats.transferMaxUs,
             m_stats.normalizedVideoFrames,
             averageUs(m_stats.normalizeTotalUs, m_stats.normalizedVideoFrames),
             m_stats.normalizeMaxUs,
             m_stats.queuedVideoFrames,
             m_stats.queueDroppedVideoFrames,
             pixelFormatName(m_stats.sourcePixelFormat),
             pixelFormatName(m_stats.cpuPixelFormat));

    m_stats = {};
    m_logTimer.restart();
}
