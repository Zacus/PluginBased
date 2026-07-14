#include "media_sdk/runtime/PlaybackRate.h"

#include <cmath>
#include <limits>

namespace media_sdk::runtime {
namespace {

std::chrono::microseconds scaledDuration(std::chrono::microseconds duration,
                                         long double scale) noexcept
{
    const auto scaled = static_cast<long double>(duration.count()) * scale;
    const auto minimum = static_cast<long double>(std::numeric_limits<std::int64_t>::min());
    const auto maximum = static_cast<long double>(std::numeric_limits<std::int64_t>::max());
    if (scaled <= minimum)
        return std::chrono::microseconds { std::numeric_limits<std::int64_t>::min() };
    if (scaled >= maximum)
        return std::chrono::microseconds { std::numeric_limits<std::int64_t>::max() };
    return std::chrono::microseconds { static_cast<std::int64_t>(std::llround(scaled)) };
}

} // namespace

bool isPlaybackRateSupported(double playbackRate) noexcept
{
    return std::isfinite(playbackRate)
        && playbackRate >= kMinimumPlaybackRate
        && playbackRate <= kMaximumPlaybackRate;
}

bool playbackRatesEqual(double lhs, double rhs) noexcept
{
    if (!isPlaybackRateSupported(lhs) || !isPlaybackRateSupported(rhs))
        return false;
    return playbackRateMillionths(lhs) == playbackRateMillionths(rhs);
}

std::uint32_t playbackRateMillionths(double playbackRate) noexcept
{
    if (!isPlaybackRateSupported(playbackRate))
        return kPlaybackRateScale;
    return static_cast<std::uint32_t>(
        std::llround(playbackRate * static_cast<double>(kPlaybackRateScale)));
}

std::chrono::microseconds mediaDurationForPlaybackDuration(
    std::chrono::microseconds playbackDuration,
    double playbackRate) noexcept
{
    if (!isPlaybackRateSupported(playbackRate))
        return playbackDuration;
    return scaledDuration(playbackDuration, static_cast<long double>(playbackRate));
}

std::chrono::microseconds playbackDurationForMediaDuration(
    std::chrono::microseconds mediaDuration,
    double playbackRate) noexcept
{
    if (!isPlaybackRateSupported(playbackRate))
        return mediaDuration;
    return scaledDuration(mediaDuration, 1.0L / static_cast<long double>(playbackRate));
}

} // namespace media_sdk::runtime
