#include "AvSyncScheduler.h"
#include "MasterClock.h"

#include "media_sdk/runtime/AudioOutput.h"

#include <cassert>
#include <chrono>

using namespace std::chrono_literals;

namespace {

constexpr auto submitLeadTime = std::chrono::milliseconds(2);
constexpr auto lateDropThreshold = std::chrono::milliseconds(100);
constexpr auto maxScheduledWait = std::chrono::milliseconds(40);
constexpr int maxConsecutiveDropsBeforeForceRender = 8;

media_sdk::runtime::ClockSnapshot audioClock(
    std::chrono::microseconds position,
    media_sdk::runtime::Generation generation,
    bool valid = true)
{
    return media_sdk::runtime::ClockSnapshot {
        .position = position,
        .hardwareLatency = 0us,
        .queuedDuration = 0us,
        .generation = generation,
        .valid = valid,
        .paused = false,
    };
}

void rendersFrameInsideThreshold()
{
    media_sdk::runtime::AvSyncScheduler scheduler({
        .submitLeadTime = submitLeadTime,
        .lateDropThreshold = lateDropThreshold,
        .maxScheduledWait = maxScheduledWait,
        .maxConsecutiveDropsBeforeForceRender = maxConsecutiveDropsBeforeForceRender,
    });
    scheduler.reset(4);

    const auto decision = scheduler.decide(101ms, audioClock(100ms, 4), 4);
    assert(decision.action == media_sdk::runtime::VideoScheduleAction::Render);
    assert(decision.waitTime == 0us);
    assert(decision.forcedRender == false);
}

void waitsForEarlyFrameWithSubmitLeadTime()
{
    media_sdk::runtime::AvSyncScheduler scheduler({
        .submitLeadTime = submitLeadTime,
        .lateDropThreshold = lateDropThreshold,
        .maxScheduledWait = maxScheduledWait,
        .maxConsecutiveDropsBeforeForceRender = maxConsecutiveDropsBeforeForceRender,
    });
    scheduler.reset(4);

    const auto leadDecision = scheduler.decide(110ms, audioClock(100ms, 4), 4);
    assert(leadDecision.action == media_sdk::runtime::VideoScheduleAction::Wait);
    assert(leadDecision.waitTime == 8ms);

    const auto cappedDecision = scheduler.decide(200ms, audioClock(100ms, 4), 4);
    assert(cappedDecision.action == media_sdk::runtime::VideoScheduleAction::Wait);
    assert(cappedDecision.waitTime == maxScheduledWait);
}

void dropsLateFrameBeyondThreshold()
{
    media_sdk::runtime::AvSyncScheduler scheduler({
        .submitLeadTime = submitLeadTime,
        .lateDropThreshold = lateDropThreshold,
        .maxScheduledWait = maxScheduledWait,
        .maxConsecutiveDropsBeforeForceRender = maxConsecutiveDropsBeforeForceRender,
    });
    scheduler.reset(4);

    const auto decision = scheduler.decide(100ms, audioClock(250ms, 4), 4);
    assert(decision.action == media_sdk::runtime::VideoScheduleAction::Drop);
    assert(decision.lateness == 150ms);
    assert(decision.forcedRender == false);
}

void forcesRenderAfterEightConsecutiveDrops()
{
    media_sdk::runtime::AvSyncScheduler scheduler({
        .submitLeadTime = submitLeadTime,
        .lateDropThreshold = lateDropThreshold,
        .maxScheduledWait = maxScheduledWait,
        .maxConsecutiveDropsBeforeForceRender = maxConsecutiveDropsBeforeForceRender,
    });
    scheduler.reset(4);

    for (int i = 0; i < maxConsecutiveDropsBeforeForceRender; ++i) {
        const auto decision = scheduler.decide(100ms, audioClock(250ms + std::chrono::milliseconds(i), 4), 4);
        assert(decision.action == media_sdk::runtime::VideoScheduleAction::Drop);
        assert(decision.forcedRender == false);
    }

    const auto forced = scheduler.decide(100ms, audioClock(260ms, 4), 4);
    assert(forced.action == media_sdk::runtime::VideoScheduleAction::Render);
    assert(forced.forcedRender == true);

    const auto nextLateFrame = scheduler.decide(100ms, audioClock(270ms, 4), 4);
    assert(nextLateFrame.action == media_sdk::runtime::VideoScheduleAction::Drop);
    assert(nextLateFrame.forcedRender == false);
}

void usesVideoMonotonicClockWhenAudioClockInvalid()
{
    media_sdk::runtime::AvSyncScheduler scheduler({
        .submitLeadTime = submitLeadTime,
        .lateDropThreshold = lateDropThreshold,
        .maxScheduledWait = maxScheduledWait,
        .maxConsecutiveDropsBeforeForceRender = maxConsecutiveDropsBeforeForceRender,
    });
    scheduler.reset(4);

    const auto decision = scheduler.decide(500ms, audioClock(10s, 4, false), 4);
    assert(decision.action == media_sdk::runtime::VideoScheduleAction::Render);
    assert(decision.lateness == 0us);
}

void ignoresAudioClockFromOldGeneration()
{
    media_sdk::runtime::AvSyncScheduler scheduler({
        .submitLeadTime = submitLeadTime,
        .lateDropThreshold = lateDropThreshold,
        .maxScheduledWait = maxScheduledWait,
        .maxConsecutiveDropsBeforeForceRender = maxConsecutiveDropsBeforeForceRender,
    });
    scheduler.reset(9);

    const auto decision = scheduler.decide(500ms, audioClock(10s, 8), 9);
    assert(decision.action == media_sdk::runtime::VideoScheduleAction::Render);
    assert(decision.lateness == 0us);
}

void smoothsVideoOnlyClockAgainstSingleDelayedObservation()
{
    auto now = media_sdk::runtime::MasterClock::SteadyClock::time_point {};
    media_sdk::runtime::MasterClock clock([&now]()
    {
        return now;
    });
    clock.reset(4);

    const auto invalidClock = audioClock(0us, 4, false);
    const auto anchored = clock.positionForVideoFrame(0ms, invalidClock, 4);
    assert(anchored == 0ms);

    now += 150ms;
    const auto smoothed = clock.positionForVideoFrame(40ms, invalidClock, 4);
    assert(smoothed - 40ms < lateDropThreshold);
}

void repeatedVideoOnlyDecisionForSameFrameDoesNotCompoundSmoothing()
{
    auto now = media_sdk::runtime::MasterClock::SteadyClock::time_point {};
    media_sdk::runtime::MasterClock clock([&now]()
    {
        return now;
    });
    clock.reset(4);

    const auto invalidClock = audioClock(0us, 4, false);
    (void)clock.positionForVideoFrame(0ms, invalidClock, 4);

    now += 150ms;
    const auto firstDecisionPosition = clock.positionForVideoFrame(40ms, invalidClock, 4);
    now += 10ms;
    const auto repeatedDecisionPosition = clock.positionForVideoFrame(40ms, invalidClock, 4);
    assert(repeatedDecisionPosition == firstDecisionPosition + 10ms);
}

} // namespace

int main()
{
    rendersFrameInsideThreshold();
    waitsForEarlyFrameWithSubmitLeadTime();
    dropsLateFrameBeyondThreshold();
    forcesRenderAfterEightConsecutiveDrops();
    usesVideoMonotonicClockWhenAudioClockInvalid();
    ignoresAudioClockFromOldGeneration();
    smoothsVideoOnlyClockAgainstSingleDelayedObservation();
    repeatedVideoOnlyDecisionForSameFrameDoesNotCompoundSmoothing();
}
