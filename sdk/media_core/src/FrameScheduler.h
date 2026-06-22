#pragma once

#include <chrono>
#include <optional>

namespace media_sdk {

enum class FrameScheduleAction {
    Render,
    Wait,
    Drop
};

struct FrameScheduleDecision {
    FrameScheduleAction action = FrameScheduleAction::Render;
    std::chrono::microseconds wait { 0 };
    std::chrono::microseconds diff { 0 };
};

class FrameScheduler
{
public:
    static constexpr std::chrono::microseconds SubmitLeadTime { 2'000 };
    static constexpr std::chrono::microseconds LateDropThreshold { 100'000 };

    static FrameScheduleDecision decide(
        std::chrono::microseconds framePts,
        std::optional<std::chrono::microseconds> clock);
};

} // namespace media_sdk
