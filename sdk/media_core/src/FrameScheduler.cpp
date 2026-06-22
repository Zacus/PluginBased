#include "FrameScheduler.h"

namespace media_sdk {

FrameScheduleDecision FrameScheduler::decide(
    std::chrono::microseconds framePts,
    std::optional<std::chrono::microseconds> clock)
{
    if (!clock)
        return {};

    const auto diff = framePts - *clock;
    if (diff > SubmitLeadTime)
        return { FrameScheduleAction::Wait, diff - SubmitLeadTime, diff };

    if (diff < -LateDropThreshold)
        return { FrameScheduleAction::Drop, std::chrono::microseconds { 0 }, diff };

    return { FrameScheduleAction::Render, std::chrono::microseconds { 0 }, diff };
}

} // namespace media_sdk
