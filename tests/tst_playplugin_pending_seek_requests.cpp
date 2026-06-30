#include "playback/PendingSeekRequests.h"

#include <cassert>
#include <chrono>

using namespace std::chrono_literals;

void exactCompletionTakesMatchingRequest()
{
    PendingSeekRequests<int> requests;
    requests.push(100ms, 1);

    const auto completed = requests.takeForCompletedPosition(100ms);

    assert(completed);
    assert(*completed == 1);
    assert(requests.empty());
}

void coalescedCompletionTakesLatestRequestAndDropsOlderRequests()
{
    PendingSeekRequests<int> requests;
    requests.push(100ms, 1);
    requests.push(150ms, 2);
    requests.push(200ms, 3);

    const auto completed = requests.takeForCompletedPosition(200ms);

    assert(completed);
    assert(*completed == 3);
    assert(requests.empty());
}

void queuedNonCoalescedCompletionKeepsLaterRequests()
{
    PendingSeekRequests<int> requests;
    requests.push(100ms, 1);
    requests.push(200ms, 2);

    const auto first = requests.takeForCompletedPosition(100ms);
    const auto second = requests.takeForCompletedPosition(200ms);

    assert(first);
    assert(*first == 1);
    assert(second);
    assert(*second == 2);
    assert(requests.empty());
}

void duplicatePositionCompletionUsesLatestMatchingRequest()
{
    PendingSeekRequests<int> requests;
    requests.push(100ms, 1);
    requests.push(200ms, 2);
    requests.push(100ms, 3);

    const auto completed = requests.takeForCompletedPosition(100ms);

    assert(completed);
    assert(*completed == 3);
    assert(requests.empty());
}

void unmatchedCompletionIsIgnored()
{
    PendingSeekRequests<int> requests;
    requests.push(100ms, 1);

    const auto completed = requests.takeForCompletedPosition(200ms);

    assert(!completed);
    assert(!requests.empty());
}

int main()
{
    exactCompletionTakesMatchingRequest();
    coalescedCompletionTakesLatestRequestAndDropsOlderRequests();
    queuedNonCoalescedCompletionKeepsLaterRequests();
    duplicatePositionCompletionUsesLatestMatchingRequest();
    unmatchedCompletionIsIgnored();
    return 0;
}
