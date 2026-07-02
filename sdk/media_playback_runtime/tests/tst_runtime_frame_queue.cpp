#include "RuntimeFrameQueue.h"

#include "media_sdk/runtime/RuntimeTypes.h"

#include <cassert>
#include <chrono>
#include <future>
#include <thread>

using namespace std::chrono_literals;

namespace {

media_sdk::runtime::RuntimeVideoFrame makeFrame(
    media_sdk::runtime::SessionId sessionId,
    media_sdk::runtime::Generation generation,
    int width)
{
    media_sdk::runtime::RuntimeVideoFrame frame;
    frame.sessionId = sessionId;
    frame.generation = generation;
    frame.frame = media_sdk::VideoFrame(media_sdk::VideoFrameDesc {
        .width = width,
        .height = 720,
        .pixelFormat = media_sdk::PixelFormat::Yuv420P,
        .pts = std::chrono::microseconds(width),
    });
    return frame;
}

void testPushPopPreservesAcceptedOrder()
{
    media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame> queue(3);
    queue.reset(10, 2);

    assert(queue.push(makeFrame(10, 2, 100)).status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);
    assert(queue.push(makeFrame(10, 2, 200)).status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);
    assert(queue.size() == 2);

    media_sdk::runtime::RuntimeVideoFrame popped;
    assert(queue.waitPop(popped) == media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame>::PopResult::Frame);
    assert(popped.frame.width() == 100);
    assert(queue.waitPop(popped) == media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame>::PopResult::Frame);
    assert(popped.frame.width() == 200);
    assert(queue.size() == 0);
}

void testQueueRejectsOldGeneration()
{
    media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame> queue(1);
    queue.reset(10, 3);

    const auto staleResult = queue.push(makeFrame(10, 2, 100));
    assert(staleResult.status == media_sdk::runtime::RuntimeFramePushStatus::RejectedGeneration);
    assert(queue.size() == 0);

    const auto wrongSessionResult = queue.push(makeFrame(11, 3, 100));
    assert(wrongSessionResult.status == media_sdk::runtime::RuntimeFramePushStatus::RejectedGeneration);
    assert(queue.size() == 0);
}

void testEofIsDeliveredAfterAcceptedFrames()
{
    media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame> queue(4);
    queue.reset(10, 4);

    assert(queue.push(makeFrame(10, 4, 100)).status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);
    assert(queue.push(makeFrame(10, 4, 200)).status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);
    assert(queue.pushEndOfStream(10, 4).status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);

    media_sdk::runtime::RuntimeVideoFrame popped;
    assert(queue.waitPop(popped) == media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame>::PopResult::Frame);
    assert(popped.frame.width() == 100);
    assert(queue.waitPop(popped) == media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame>::PopResult::Frame);
    assert(popped.frame.width() == 200);
    assert(queue.waitPop(popped) == media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame>::PopResult::EndOfStream);
}

void testAbortWakesWaitersAndClearsPendingFrames()
{
    media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame> queue(3);
    queue.reset(10, 5);
    assert(queue.push(makeFrame(10, 5, 100)).status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);
    assert(queue.size() == 1);

    queue.abort();
    assert(queue.size() == 0);
    assert(queue.abortCount() == 1);

    queue.reset(10, 6);
    std::promise<void> popStarted;
    auto popStartedFuture = popStarted.get_future();
    auto result = std::async(std::launch::async, [&queue, &popStarted]() {
        popStarted.set_value();
        media_sdk::runtime::RuntimeVideoFrame popped;
        return queue.waitPop(popped);
    });
    popStartedFuture.wait();
    std::this_thread::sleep_for(20ms);
    queue.abort();
    assert(result.wait_for(1s) == std::future_status::ready);
    assert(result.get() == media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame>::PopResult::Aborted);
    assert(queue.abortCount() == 2);
}

void testFinishDoesNotAcceptMoreFrames()
{
    media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame> queue(2);
    queue.reset(10, 6);
    queue.finish();

    media_sdk::runtime::RuntimeVideoFrame popped;
    assert(queue.waitPop(popped) == media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame>::PopResult::Closed);
    assert(queue.push(makeFrame(10, 6, 100)).status == media_sdk::runtime::RuntimeFramePushStatus::Closed);

    queue.reset(10, 7);
    assert(queue.generation() == 7);
    assert(queue.push(makeFrame(10, 7, 200)).status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);
    assert(queue.waitPop(popped) == media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame>::PopResult::Frame);
    assert(popped.generation == 7);
}

void pushWaitIsCancelledByAbort()
{
    media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame> queue(1);
    queue.reset(10, 2);
    assert(queue.push(makeFrame(10, 2, 100)).status
        == media_sdk::runtime::RuntimeFramePushStatus::Accepted);

    auto future = std::async(std::launch::async, [&queue]() {
        return queue.push(makeFrame(10, 2, 200));
    });

    std::this_thread::sleep_for(20ms);
    queue.abort();
    const auto result = future.get();
    assert(result.status == media_sdk::runtime::RuntimeFramePushStatus::Cancelled);
}

void pushReportsBackpressureWhenItWaited()
{
    media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame> queue(1);
    queue.reset(10, 2);
    assert(queue.push(makeFrame(10, 2, 100)).status
        == media_sdk::runtime::RuntimeFramePushStatus::Accepted);

    auto future = std::async(std::launch::async, [&queue]() {
        return queue.push(makeFrame(10, 2, 200));
    });

    std::this_thread::sleep_for(20ms);
    media_sdk::runtime::RuntimeVideoFrame popped;
    assert(queue.waitPop(popped) == media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame>::PopResult::Frame);

    const auto result = future.get();
    assert(result.status == media_sdk::runtime::RuntimeFramePushStatus::Backpressured);
    assert(result.waitTime > std::chrono::microseconds { 0 });
    assert(queue.highWatermark() == 1);
}

void eofPushReportsBackpressureWhenItWaited()
{
    media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame> queue(1);
    queue.reset(10, 2);
    assert(queue.push(makeFrame(10, 2, 100)).status
        == media_sdk::runtime::RuntimeFramePushStatus::Accepted);

    auto future = std::async(std::launch::async, [&queue]() {
        return queue.pushEndOfStream(10, 2);
    });

    std::this_thread::sleep_for(20ms);
    media_sdk::runtime::RuntimeVideoFrame popped;
    assert(queue.waitPop(popped) == media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame>::PopResult::Frame);

    const auto result = future.get();
    assert(result.status == media_sdk::runtime::RuntimeFramePushStatus::Backpressured);
    assert(result.waitTime > std::chrono::microseconds { 0 });
}

void eofPushWaitIsCancelledByAbort()
{
    media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame> queue(1);
    queue.reset(10, 2);
    assert(queue.push(makeFrame(10, 2, 100)).status
        == media_sdk::runtime::RuntimeFramePushStatus::Accepted);

    auto future = std::async(std::launch::async, [&queue]() {
        return queue.pushEndOfStream(10, 2);
    });

    std::this_thread::sleep_for(20ms);
    queue.abort();
    const auto result = future.get();
    assert(result.status == media_sdk::runtime::RuntimeFramePushStatus::Cancelled);
    assert(result.waitTime > std::chrono::microseconds { 0 });
}

void blockedPushRejectsFrameWhenResetChangesGeneration()
{
    media_sdk::runtime::RuntimeFrameQueue<media_sdk::runtime::RuntimeVideoFrame> queue(1);
    queue.reset(10, 2);
    assert(queue.push(makeFrame(10, 2, 100)).status
        == media_sdk::runtime::RuntimeFramePushStatus::Accepted);

    auto future = std::async(std::launch::async, [&queue]() {
        return queue.push(makeFrame(10, 2, 200));
    });

    std::this_thread::sleep_for(20ms);
    queue.reset(10, 3);

    const auto result = future.get();
    assert(result.status == media_sdk::runtime::RuntimeFramePushStatus::RejectedGeneration);
    assert(queue.size() == 0);
}

} // namespace

int main()
{
    testPushPopPreservesAcceptedOrder();
    testQueueRejectsOldGeneration();
    testEofIsDeliveredAfterAcceptedFrames();
    testAbortWakesWaitersAndClearsPendingFrames();
    testFinishDoesNotAcceptMoreFrames();
    pushWaitIsCancelledByAbort();
    pushReportsBackpressureWhenItWaited();
    eofPushReportsBackpressureWhenItWaited();
    eofPushWaitIsCancelledByAbort();
    blockedPushRejectsFrameWhenResetChangesGeneration();
}
