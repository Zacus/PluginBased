#pragma once

#include "media_sdk/runtime/RuntimeTypes.h"
#include "media_sdk/runtime/VideoPresenter.h"

#include <cstddef>
#include <deque>

namespace media_sdk::runtime {

enum class PresentCompletionAction {
    AcceptedSuccess,
    AcceptedFailure,
    IgnoredStale,
    IgnoredUnknown
};

enum class PresentTrackAction {
    TrackedPending,
    ConsumedSuccess,
    ConsumedFailure,
    Rejected
};

struct TrackedPresent {
    PresentId id = 0;
    SessionId sessionId = 0;
    Generation generation = 0;
    bool nativeFrame = false;
};

struct PresentTrackResult {
    PresentTrackAction action = PresentTrackAction::Rejected;
    PresentStatus completionStatus = PresentStatus::Failed;
    PresentDiagnostics diagnostics {};
};

class PresentTracker
{
public:
    void reset(SessionId sessionId, Generation generation);
    void setMaxPending(std::size_t maxPending);
    [[nodiscard("track result determines whether presenter backpressure accepted the frame")]]
    bool track(TrackedPresent present);
    [[nodiscard("track result distinguishes pending presents from early completions")]]
    PresentTrackResult trackResult(TrackedPresent present);
    [[nodiscard("completion action drives fallback, stale completion handling, and backpressure release")]]
    PresentCompletionAction complete(SessionId sessionId, Generation generation, PresentCompletion completion);
    void clear();
    [[nodiscard]]
    bool hasCapacity() const;
    [[nodiscard]]
    std::size_t pendingCount() const;

private:
    using PendingList = std::deque<TrackedPresent>;
    using CompletionList = std::deque<PresentCompletion>;

    [[nodiscard]]
    PendingList::iterator findPending(SessionId sessionId, Generation generation, PresentId id);
    [[nodiscard]]
    CompletionList::iterator findEarlyCompletion(PresentId id);
    [[nodiscard]]
    bool isCurrent(SessionId sessionId, Generation generation) const;
    [[nodiscard]]
    static bool isFailureStatus(PresentStatus status);

    SessionId m_sessionId = 0;
    Generation m_generation = 0;
    std::size_t m_maxPending = 1;
    PendingList m_pending;
    CompletionList m_earlyCompletions;
};

} // namespace media_sdk::runtime
