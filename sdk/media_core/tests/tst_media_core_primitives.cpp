#include "ClockSync.h"
#include "FrameQueue.h"
#include "FrameScheduler.h"

#include <cassert>
#include <chrono>
#include <optional>
#include <thread>

using namespace std::chrono_literals;

namespace {

void testFrameScheduler()
{
    const auto invalid = media_sdk::FrameScheduler::decide(1'000'000us,
                                                           std::nullopt);
    assert(invalid.action == media_sdk::FrameScheduleAction::Render);
    assert(invalid.wait == 0us);

    const auto early = media_sdk::FrameScheduler::decide(1'050'000us,
                                                         1'000'000us);
    assert(early.action == media_sdk::FrameScheduleAction::Wait);
    assert(early.diff == 50'000us);
    assert(early.wait == 48'000us);

    const auto insideLead = media_sdk::FrameScheduler::decide(1'001'500us,
                                                              1'000'000us);
    assert(insideLead.action == media_sdk::FrameScheduleAction::Render);

    const auto late = media_sdk::FrameScheduler::decide(900'000us,
                                                        1'001'000us);
    assert(late.action == media_sdk::FrameScheduleAction::Drop);
}

void testClockSync()
{
    media_sdk::ClockSync clock;
    assert(!clock.currentTime().has_value());

    clock.setAudioClock(10'000us);
    const auto first = clock.currentTime();
    assert(first.has_value());
    assert(*first >= 10'000us);

    clock.setPaused(true);
    const auto paused = clock.currentTime();
    std::this_thread::sleep_for(2ms);
    assert(clock.currentTime() == paused);

    clock.setPaused(false);
    std::this_thread::sleep_for(2ms);
    assert(clock.currentTime() > paused);

    clock.invalidate();
    assert(!clock.currentTime().has_value());
}

void testFrameQueue()
{
    media_sdk::FrameQueue<int> queue(2);
    bool wakeCalled = false;
    queue.setWakeCallback([&wakeCalled]()
    {
        wakeCalled = true;
    });

    assert(queue.tryPush(42, 7));
    assert(wakeCalled);
    assert(queue.size() == 1);

    media_sdk::FrameQueue<int>::Entry entry;
    assert(queue.tryPop(entry));
    assert(entry.frame == 42);
    assert(entry.serial == 7);
    assert(!entry.eof);

    assert(queue.tryPush(1));
    assert(queue.tryPush(2));
    assert(!queue.tryPush(3));

    queue.flush();
    assert(queue.empty());
    assert(queue.flushSerial() == 1);

    queue.abort();
    assert(!queue.push(9));
    queue.resetAbort();
    assert(queue.tryPush(10));
    assert(queue.pop(entry));
    assert(entry.frame == 10);
}

}

int main()
{
    testFrameScheduler();
    testClockSync();
    testFrameQueue();
    return 0;
}
