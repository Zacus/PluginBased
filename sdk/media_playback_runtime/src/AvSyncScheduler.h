#pragma once

#include "MasterClock.h"
#include "media_sdk/runtime/AudioOutput.h"
#include "media_sdk/runtime/RuntimeTypes.h"

#include <chrono>

namespace media_sdk::runtime {

enum class VideoScheduleAction {
    Render,
    Wait,
    Drop
};

struct VideoScheduleDecision {
    VideoScheduleAction action = VideoScheduleAction::Wait;
    std::chrono::microseconds waitTime { 0 };
    std::chrono::microseconds lateness { 0 };
    bool forcedRender = false;
};

struct AvSyncConfig {
    std::chrono::microseconds submitLeadTime { std::chrono::milliseconds(2) };
    std::chrono::microseconds lateDropThreshold { std::chrono::milliseconds(100) };
    std::chrono::microseconds maxScheduledWait { std::chrono::milliseconds(40) };
    int maxConsecutiveDropsBeforeForceRender = 8;
};

class AvSyncScheduler
{
public:
    explicit AvSyncScheduler(AvSyncConfig config = {});

    void reset(Generation generation);
    VideoScheduleDecision decide(
        std::chrono::microseconds framePts,
        ClockSnapshot clock,
        Generation currentGeneration);

private:
    AvSyncConfig m_config;
    MasterClock m_masterClock;
    int m_consecutiveDrops = 0;
};

} // namespace media_sdk::runtime
