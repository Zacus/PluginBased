#include "MasterClock.h"

namespace media_sdk::runtime {

void MasterClock::reset(Generation generation)
{
    m_generation = generation;
    m_videoAnchorValid = false;
    m_videoAnchorPts = std::chrono::microseconds { 0 };
    m_videoAnchorTime = {};
}

std::chrono::microseconds MasterClock::positionForVideoFrame(
    std::chrono::microseconds framePts,
    ClockSnapshot audioClock,
    Generation currentGeneration)
{
    if (audioClock.valid && audioClock.generation == currentGeneration)
        return audioClock.position;

    if (m_generation != currentGeneration || !m_videoAnchorValid) {
        m_generation = currentGeneration;
        m_videoAnchorValid = true;
        m_videoAnchorPts = framePts;
        m_videoAnchorTime = SteadyClock::now();
        return m_videoAnchorPts;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        SteadyClock::now() - m_videoAnchorTime);
    return m_videoAnchorPts + elapsed;
}

} // namespace media_sdk::runtime
