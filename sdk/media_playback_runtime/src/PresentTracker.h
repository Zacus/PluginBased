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

struct TrackedPresent {
    PresentId id = 0;
    SessionId sessionId = 0;
    Generation generation = 0;
    bool nativeFrame = false;
};

class PresentTracker
{
public:
    void reset(SessionId sessionId, Generation generation);
    void setMaxPending(std::size_t maxPending);
    bool track(TrackedPresent present);
    PresentCompletionAction complete(SessionId sessionId, Generation generation, PresentCompletion completion);
    void clear();
    std::size_t pendingCount() const;

private:
    using PendingList = std::deque<TrackedPresent>;

    PendingList::iterator findPending(SessionId sessionId, Generation generation, PresentId id);
    bool isCurrent(SessionId sessionId, Generation generation) const;
    static bool isFailureStatus(PresentStatus status);

    SessionId m_sessionId = 0;
    Generation m_generation = 0;
    std::size_t m_maxPending = 1;
    PendingList m_pending;
};

} // namespace media_sdk::runtime
