#include "common/FrameQueue.h"

#include <cassert>
#include <chrono>
#include <future>

using namespace std::chrono_literals;

void cancelPendingPushesRejectsBlockedProducerWithoutAbortingConsumer()
{
    FrameQueue<int> queue(1);
    assert(queue.push(1, 1));

    auto blockedPush = std::async(std::launch::async, [&queue]() {
        return queue.push(2, 1);
    });
    assert(blockedPush.wait_for(20ms) == std::future_status::timeout);

    queue.cancelPendingPushes();
    assert(blockedPush.wait_for(1s) == std::future_status::ready);
    assert(!blockedPush.get());

    auto blockedPop = std::async(std::launch::async, [&queue]() {
        FrameQueue<int>::Entry entry;
        const bool popped = queue.pop(entry);
        return popped && entry.frame == 3 && entry.serial == 2;
    });
    assert(blockedPop.wait_for(20ms) == std::future_status::timeout);

    assert(queue.push(3, 2));
    assert(blockedPop.wait_for(1s) == std::future_status::ready);
    assert(blockedPop.get());
}

void abortStillStopsBlockedConsumer()
{
    FrameQueue<int> queue(1);
    auto blockedPop = std::async(std::launch::async, [&queue]() {
        FrameQueue<int>::Entry entry;
        return queue.pop(entry);
    });
    assert(blockedPop.wait_for(20ms) == std::future_status::timeout);

    queue.abort();
    assert(blockedPop.wait_for(1s) == std::future_status::ready);
    assert(!blockedPop.get());
}

void pushIfCancelSerialRejectsPushAfterCancel()
{
    FrameQueue<int> queue(1);
    const int cancelSerial = queue.cancelSerial();

    queue.cancelPendingPushes();

    assert(!queue.pushIfCancelSerial(1, cancelSerial, 1));
    assert(queue.empty());
}

void finishIfCancelSerialRejectsEofAfterCancel()
{
    FrameQueue<int> queue(1);
    const int cancelSerial = queue.cancelSerial();

    queue.cancelPendingPushes();

    assert(!queue.finishIfCancelSerial(cancelSerial, 1));
    assert(queue.empty());
}

int main()
{
    cancelPendingPushesRejectsBlockedProducerWithoutAbortingConsumer();
    abortStillStopsBlockedConsumer();
    pushIfCancelSerialRejectsPushAfterCancel();
    finishIfCancelSerialRejectsEofAfterCancel();
    return 0;
}
