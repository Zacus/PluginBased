#include "PresentTracker.h"

#include <cassert>

namespace {

media_sdk::runtime::TrackedPresent makePresent(
    media_sdk::runtime::PresentId id,
    media_sdk::runtime::SessionId sessionId,
    media_sdk::runtime::Generation generation,
    bool nativeFrame = false)
{
    return media_sdk::runtime::TrackedPresent {
        .id = id,
        .sessionId = sessionId,
        .generation = generation,
        .nativeFrame = nativeFrame,
    };
}

media_sdk::runtime::PresentCompletion completion(
    media_sdk::runtime::PresentId id,
    media_sdk::runtime::PresentStatus status)
{
    return media_sdk::runtime::PresentCompletion {
        .id = id,
        .status = status,
        .detail = {},
    };
}

void acceptsCompletionForCurrentSessionGenerationAndPresentId()
{
    media_sdk::runtime::PresentTracker tracker;
    tracker.reset(10, 4);
    assert(tracker.track(makePresent(1, 10, 4)));
    assert(tracker.pendingCount() == 1);

    const auto action = tracker.complete(10, 4, completion(1, media_sdk::runtime::PresentStatus::Presented));
    assert(action == media_sdk::runtime::PresentCompletionAction::AcceptedSuccess);
    assert(tracker.pendingCount() == 0);
}

void ignoresCompletionFromOldGeneration()
{
    media_sdk::runtime::PresentTracker tracker;
    tracker.reset(10, 4);
    assert(tracker.track(makePresent(1, 10, 4)));

    const auto action = tracker.complete(10, 3, completion(1, media_sdk::runtime::PresentStatus::DeviceLost));
    assert(action == media_sdk::runtime::PresentCompletionAction::IgnoredStale);
    assert(tracker.pendingCount() == 1);
}

void ignoresUnknownPresentId()
{
    media_sdk::runtime::PresentTracker tracker;
    tracker.reset(10, 4);
    assert(tracker.track(makePresent(1, 10, 4)));

    const auto action = tracker.complete(10, 4, completion(2, media_sdk::runtime::PresentStatus::Presented));
    assert(action == media_sdk::runtime::PresentCompletionAction::IgnoredUnknown);
    assert(tracker.pendingCount() == 1);
}

void reportsFailureForCurrentNativePresent()
{
    media_sdk::runtime::PresentTracker tracker;
    tracker.reset(10, 4);
    assert(tracker.track(makePresent(1, 10, 4, true)));

    const auto unsupported = tracker.complete(10, 4, completion(1, media_sdk::runtime::PresentStatus::UnsupportedNativeHandle));
    assert(unsupported == media_sdk::runtime::PresentCompletionAction::AcceptedFailure);
    assert(tracker.pendingCount() == 0);

    assert(tracker.track(makePresent(2, 10, 4, true)));
    const auto deviceLost = tracker.complete(10, 4, completion(2, media_sdk::runtime::PresentStatus::DeviceLost));
    assert(deviceLost == media_sdk::runtime::PresentCompletionAction::AcceptedFailure);

    assert(tracker.track(makePresent(3, 10, 4, true)));
    const auto failed = tracker.complete(10, 4, completion(3, media_sdk::runtime::PresentStatus::Failed));
    assert(failed == media_sdk::runtime::PresentCompletionAction::AcceptedFailure);
}

void capsPendingDepthAndReplacesOlderSameGenerationFrame()
{
    media_sdk::runtime::PresentTracker tracker;
    tracker.reset(10, 4);
    tracker.setMaxPending(1);

    assert(tracker.track(makePresent(1, 10, 4)));
    assert(tracker.track(makePresent(2, 10, 4)));
    assert(tracker.pendingCount() == 1);

    const auto oldCompletion = tracker.complete(10, 4, completion(1, media_sdk::runtime::PresentStatus::Presented));
    assert(oldCompletion == media_sdk::runtime::PresentCompletionAction::IgnoredUnknown);

    const auto currentCompletion = tracker.complete(10, 4, completion(2, media_sdk::runtime::PresentStatus::Presented));
    assert(currentCompletion == media_sdk::runtime::PresentCompletionAction::AcceptedSuccess);
    assert(tracker.pendingCount() == 0);
}

void clearCancelsAllPendingPresents()
{
    media_sdk::runtime::PresentTracker tracker;
    tracker.reset(10, 4);
    tracker.setMaxPending(3);
    assert(tracker.track(makePresent(1, 10, 4)));
    assert(tracker.track(makePresent(2, 10, 4)));
    assert(tracker.pendingCount() == 2);

    tracker.clear();
    assert(tracker.pendingCount() == 0);
    const auto action = tracker.complete(10, 4, completion(1, media_sdk::runtime::PresentStatus::Presented));
    assert(action == media_sdk::runtime::PresentCompletionAction::IgnoredUnknown);
}

} // namespace

int main()
{
    acceptsCompletionForCurrentSessionGenerationAndPresentId();
    ignoresCompletionFromOldGeneration();
    ignoresUnknownPresentId();
    reportsFailureForCurrentNativePresent();
    capsPendingDepthAndReplacesOlderSameGenerationFrame();
    clearCancelsAllPendingPresents();
}
