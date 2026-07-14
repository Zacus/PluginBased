#include "media_sdk/runtime/PlaybackRate.h"

#include <cassert>
#include <chrono>
#include <limits>

using namespace std::chrono_literals;

namespace {

void validatesSupportedRange()
{
    using namespace media_sdk::runtime;
    assert(isPlaybackRateSupported(0.5));
    assert(isPlaybackRateSupported(1.0));
    assert(isPlaybackRateSupported(2.0));
    assert(!isPlaybackRateSupported(0.499));
    assert(!isPlaybackRateSupported(2.001));
    assert(!isPlaybackRateSupported(std::numeric_limits<double>::infinity()));
    assert(!isPlaybackRateSupported(std::numeric_limits<double>::quiet_NaN()));
}

void comparesAtClockPrecision()
{
    using namespace media_sdk::runtime;
    assert(playbackRatesEqual(1.25, 1.2500004));
    assert(!playbackRatesEqual(1.25, 1.250002));
    assert(!playbackRatesEqual(1.0, std::numeric_limits<double>::quiet_NaN()));
}

void convertsBetweenMediaAndPlaybackDurations()
{
    using namespace media_sdk::runtime;
    assert(mediaDurationForPlaybackDuration(100ms, 0.5) == 50ms);
    assert(mediaDurationForPlaybackDuration(100ms, 2.0) == 200ms);
    assert(playbackDurationForMediaDuration(100ms, 0.5) == 200ms);
    assert(playbackDurationForMediaDuration(100ms, 2.0) == 50ms);
}

} // namespace

int main()
{
    validatesSupportedRange();
    comparesAtClockPrecision();
    convertsBetweenMediaAndPlaybackDurations();
}
