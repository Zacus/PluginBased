#pragma once

#include "media_sdk/runtime/AudioOutput.h"
#include "media_sdk/runtime/RuntimeTypes.h"

#include <chrono>
#include <functional>
#include <optional>

namespace media_sdk::runtime {

class MasterClock
{
public:
    using SteadyClock = std::chrono::steady_clock;
    using NowFunction = std::function<SteadyClock::time_point()>;

    explicit MasterClock(NowFunction now = {});

    void reset(Generation generation);

    [[nodiscard("master clock position is required for video scheduling")]]
    std::chrono::microseconds positionForVideoFrame(
        std::chrono::microseconds framePts,
        ClockSnapshot audioClock,
        Generation currentGeneration);

private:
    [[nodiscard]] SteadyClock::time_point now() const;
    [[nodiscard]] std::chrono::microseconds smoothedVideoPosition(
        std::chrono::microseconds framePts,
        SteadyClock::time_point currentTime);

    NowFunction m_now;
    Generation m_generation = 0;
    bool m_videoAnchorValid = false;
    std::chrono::microseconds m_videoAnchorPts { 0 };
    SteadyClock::time_point m_videoAnchorTime {};
    std::chrono::microseconds m_videoClockCorrection { 0 };
    std::optional<std::chrono::microseconds> m_lastVideoObservationPts;
};

} // namespace media_sdk::runtime
