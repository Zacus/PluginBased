#include "MasterClock.h"

#include <algorithm>
#include <utility>

namespace media_sdk::runtime {

namespace {

constexpr std::chrono::microseconds videoClockCorrectionClamp = std::chrono::milliseconds(80);
constexpr int videoClockSmoothingAlphaNumerator = 1;
constexpr int videoClockSmoothingAlphaDenominator = 4;

[[nodiscard]] std::chrono::microseconds clampCorrection(std::chrono::microseconds correction)
{
    return std::clamp(correction, -videoClockCorrectionClamp, videoClockCorrectionClamp);
}

} // namespace

MasterClock::MasterClock(NowFunction now)
    : m_now(std::move(now))
{
}

void MasterClock::reset(Generation generation)
{
    m_generation = generation;
    m_videoAnchorValid = false;
    m_videoAnchorPts = std::chrono::microseconds { 0 };
    m_videoAnchorTime = {};
    m_videoClockCorrection = std::chrono::microseconds { 0 };
    m_lastVideoObservationPts.reset();
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
        m_videoAnchorTime = now();
        m_videoClockCorrection = std::chrono::microseconds { 0 };
        m_lastVideoObservationPts = framePts;
        return m_videoAnchorPts;
    }

    return smoothedVideoPosition(framePts, now());
}

MasterClock::SteadyClock::time_point MasterClock::now() const
{
    if (m_now)
        return m_now();
    return SteadyClock::now();
}

std::chrono::microseconds MasterClock::smoothedVideoPosition(
    std::chrono::microseconds framePts,
    SteadyClock::time_point currentTime)
{
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        currentTime - m_videoAnchorTime);
    if (!m_lastVideoObservationPts.has_value() || *m_lastVideoObservationPts != framePts) {
        const auto extrapolated = m_videoAnchorPts + elapsed + m_videoClockCorrection;
        const auto error = framePts - extrapolated;
        const auto correctionDelta = std::chrono::microseconds {
            error.count() * videoClockSmoothingAlphaNumerator / videoClockSmoothingAlphaDenominator
        };
        m_videoClockCorrection = clampCorrection(m_videoClockCorrection + correctionDelta);
        m_lastVideoObservationPts = framePts;
    }
    return m_videoAnchorPts + elapsed + m_videoClockCorrection;
}

} // namespace media_sdk::runtime
