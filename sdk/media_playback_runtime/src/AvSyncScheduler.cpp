#include "AvSyncScheduler.h"

#include <algorithm>

namespace media_sdk::runtime {

AvSyncScheduler::AvSyncScheduler(AvSyncConfig config)
    : m_config(config)
{
}

void AvSyncScheduler::reset(Generation generation)
{
    m_masterClock.reset(generation);
    m_consecutiveDrops = 0;
}

VideoScheduleDecision AvSyncScheduler::decide(
    std::chrono::microseconds framePts,
    ClockSnapshot clock,
    Generation currentGeneration)
{
    const auto masterPosition = m_masterClock.positionForVideoFrame(framePts, clock, currentGeneration);
    VideoScheduleDecision decision;
    decision.lateness = masterPosition - framePts;

    if (decision.lateness > m_config.lateDropThreshold) {
        if (m_config.maxConsecutiveDropsBeforeForceRender <= 0
            || m_consecutiveDrops >= m_config.maxConsecutiveDropsBeforeForceRender) {
            m_consecutiveDrops = 0;
            decision.action = VideoScheduleAction::Render;
            decision.forcedRender = true;
            return decision;
        }

        ++m_consecutiveDrops;
        decision.action = VideoScheduleAction::Drop;
        return decision;
    }

    const auto untilFrame = framePts - masterPosition;
    if (untilFrame > m_config.submitLeadTime) {
        m_consecutiveDrops = 0;
        decision.action = VideoScheduleAction::Wait;
        decision.waitTime = std::min(
            untilFrame - m_config.submitLeadTime,
            m_config.maxScheduledWait);
        return decision;
    }

    m_consecutiveDrops = 0;
    decision.action = VideoScheduleAction::Render;
    return decision;
}

} // namespace media_sdk::runtime
