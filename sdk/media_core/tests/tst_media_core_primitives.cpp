#include "FrameQueue.h"

#include <cassert>

namespace {

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
    testFrameQueue();
    return 0;
}
