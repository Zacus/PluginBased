#include "DecodePerformance.h"

namespace media_sdk {
namespace {

std::string pixelFormatName(int format)
{
    if (format == AV_PIX_FMT_NONE)
        return "none";

    const char* name = av_get_pix_fmt_name(static_cast<AVPixelFormat>(format));
    return name ? name : "unknown";
}

std::int64_t averageUs(std::int64_t totalUs, std::int64_t count)
{
    return count > 0 ? totalUs / count : 0;
}

std::int64_t framePushCount(const DecodePerformanceStats& stats)
{
    return stats.framePushAccepted
        + stats.framePushBackpressured
        + stats.framePushStale
        + stats.framePushCancelled
        + stats.framePushClosed;
}

bool hasReportableActivity(const DecodePerformanceStats& stats)
{
    return stats.decodedVideoFrames > 0 || framePushCount(stats) > 0;
}

} // namespace

DecodePerformanceLogger::DecodePerformanceLogger(std::chrono::milliseconds interval)
    : m_interval(interval)
{
}

void DecodePerformanceLogger::reset(std::chrono::steady_clock::time_point now)
{
    m_stats = {};
    m_lastReport = now;
}

std::optional<DecodePerformanceReport> DecodePerformanceLogger::maybeCreateReport(
    std::string_view activeVideoDecoderName,
    std::chrono::steady_clock::time_point now)
{
    if (now - m_lastReport < m_interval)
        return std::nullopt;
    if (!hasReportableActivity(m_stats))
    {
        m_lastReport = now;
        return std::nullopt;
    }

    DecodePerformanceReport report;
    report.decoderName = std::string(activeVideoDecoderName);
    report.stats = m_stats;
    report.transferAverageUs = averageUs(m_stats.transferTotalUs, m_stats.transferredVideoFrames);
    report.normalizeAverageUs = averageUs(m_stats.normalizeTotalUs, m_stats.normalizedVideoFrames);
    report.framePushAverageWaitUs = averageUs(
        m_stats.framePushWaitUs,
        m_stats.framePushWaitCount);
    report.sourcePixelFormatName = pixelFormatName(m_stats.sourcePixelFormat);
    report.cpuPixelFormatName = pixelFormatName(m_stats.cpuPixelFormat);

    m_stats = {};
    m_lastReport = now;
    return report;
}

} // namespace media_sdk
