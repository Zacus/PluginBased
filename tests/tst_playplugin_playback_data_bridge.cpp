#include "playback/PlaybackDataBridge.h"

#include <cassert>
#include <chrono>
#include <future>

using namespace std::chrono_literals;

namespace {

AVFramePtr makeTestFrame()
{
    return make_frame();
}

void pushResultDistinguishesAcceptedAndStaleGeneration()
{
    VideoFrameQueue videoQueue(2);
    AudioFrameQueue audioQueue(2);
    PlaybackDataBridge bridge(&videoQueue, &audioQueue);
    bridge.reset({
        .hasAudio = true,
        .hasVideo = true,
        .sessionId = 1,
        .generation = 1,
    });

    const auto accepted = bridge.pushAudio(makeTestFrame(), 1, 1);
    assert(accepted.status == PlaybackDataBridge::PushStatus::Accepted);

    const auto stale = bridge.pushAudio(makeTestFrame(), 1, 2);
    assert(stale.status == PlaybackDataBridge::PushStatus::StaleGeneration);
}

void blockedPushReportsStaleWhenGenerationIsCancelled()
{
    VideoFrameQueue videoQueue(1);
    AudioFrameQueue audioQueue(1);
    PlaybackDataBridge bridge(&videoQueue, &audioQueue);
    bridge.reset({
        .hasAudio = true,
        .hasVideo = false,
        .sessionId = 1,
        .generation = 1,
    });

    const auto first = bridge.pushAudio(makeTestFrame(), 1, 1);
    assert(first.status == PlaybackDataBridge::PushStatus::Accepted);

    auto blocked = std::async(std::launch::async, [&bridge]() {
        return bridge.pushAudio(makeTestFrame(), 1, 1);
    });
    assert(blocked.wait_for(20ms) == std::future_status::timeout);

    bridge.cancelGeneration();
    assert(blocked.wait_for(1s) == std::future_status::ready);
    assert(blocked.get().status == PlaybackDataBridge::PushStatus::StaleGeneration);
}

void invalidFrameReportsClosed()
{
    VideoFrameQueue videoQueue(1);
    AudioFrameQueue audioQueue(1);
    PlaybackDataBridge bridge(&videoQueue, &audioQueue);
    bridge.reset({
        .hasAudio = true,
        .hasVideo = false,
        .sessionId = 1,
        .generation = 1,
    });

    const auto result = bridge.pushAudio({}, 1, 1);
    assert(result.status == PlaybackDataBridge::PushStatus::Closed);
}

} // namespace

int main()
{
    pushResultDistinguishesAcceptedAndStaleGeneration();
    blockedPushReportsStaleWhenGenerationIsCancelled();
    invalidFrameReportsClosed();
    return 0;
}
