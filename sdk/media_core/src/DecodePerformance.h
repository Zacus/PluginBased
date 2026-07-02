#pragma once

#include "FFmpegUtils.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace media_sdk {

struct DecodePerformanceStats {
    std::int64_t decodedVideoFrames = 0;
    std::int64_t hardwareVideoFrames = 0;
    std::int64_t transferredVideoFrames = 0;
    std::int64_t transferFailures = 0;
    std::int64_t transferTotalUs = 0;
    std::int64_t transferMaxUs = 0;
    std::int64_t nativeVideoFrames = 0;
    std::int64_t nativeFallbackVideoFrames = 0;
    std::int64_t normalizedVideoFrames = 0;
    std::int64_t normalizeTotalUs = 0;
    std::int64_t normalizeMaxUs = 0;
    std::int64_t queuedVideoFrames = 0;
    std::int64_t queueDroppedVideoFrames = 0;
    std::int64_t framePushAccepted = 0;
    std::int64_t framePushBackpressured = 0;
    std::int64_t framePushStale = 0;
    std::int64_t framePushCancelled = 0;
    std::int64_t framePushClosed = 0;
    std::int64_t framePushWaitCount = 0;
    std::int64_t framePushWaitUs = 0;
    std::int64_t framePushMaxWaitUs = 0;
    int sourcePixelFormat = AV_PIX_FMT_NONE;
    int cpuPixelFormat = AV_PIX_FMT_NONE;
};

struct DecodePerformanceReport {
    std::string decoderName;
    DecodePerformanceStats stats;
    std::int64_t transferAverageUs = 0;
    std::int64_t normalizeAverageUs = 0;
    std::int64_t framePushAverageWaitUs = 0;
    std::string sourcePixelFormatName;
    std::string cpuPixelFormatName;
};

class DecodePerformanceLogger
{
public:
    explicit DecodePerformanceLogger(std::chrono::milliseconds interval = std::chrono::milliseconds(2000));

    DecodePerformanceStats& stats() { return m_stats; }
    const DecodePerformanceStats& stats() const { return m_stats; }

    void reset(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    std::optional<DecodePerformanceReport> maybeCreateReport(
        std::string_view activeVideoDecoderName,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

private:
    std::chrono::milliseconds m_interval;
    std::chrono::steady_clock::time_point m_lastReport {};
    DecodePerformanceStats m_stats;
};

} // namespace media_sdk
