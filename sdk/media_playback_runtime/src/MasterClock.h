#pragma once

#include "media_sdk/runtime/AudioOutput.h"
#include "media_sdk/runtime/RuntimeTypes.h"

#include <chrono>

namespace media_sdk::runtime {

class MasterClock
{
public:
    void reset(Generation generation);

    std::chrono::microseconds positionForVideoFrame(
        std::chrono::microseconds framePts,
        ClockSnapshot audioClock,
        Generation currentGeneration);

private:
    using SteadyClock = std::chrono::steady_clock;

    Generation m_generation = 0;
    bool m_videoAnchorValid = false;
    std::chrono::microseconds m_videoAnchorPts { 0 };
    SteadyClock::time_point m_videoAnchorTime {};
};

} // namespace media_sdk::runtime
