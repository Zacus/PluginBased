#include "PresentTracker.h"

#include <algorithm>

namespace media_sdk::runtime {

void PresentTracker::reset(SessionId sessionId, Generation generation)
{
    m_sessionId = sessionId;
    m_generation = generation;
    m_pending.clear();
}

void PresentTracker::setMaxPending(std::size_t maxPending)
{
    m_maxPending = maxPending == 0 ? 1 : maxPending;
    while (m_pending.size() > m_maxPending)
        m_pending.pop_front();
}

bool PresentTracker::track(TrackedPresent present)
{
    if (!isCurrent(present.sessionId, present.generation) || present.id == 0)
        return false;

    if (findPending(present.sessionId, present.generation, present.id) != m_pending.end())
        return false;

    while (m_pending.size() >= m_maxPending)
        m_pending.pop_front();

    m_pending.push_back(present);
    return true;
}

PresentCompletionAction PresentTracker::complete(
    SessionId sessionId,
    Generation generation,
    PresentCompletion completion)
{
    if (!isCurrent(sessionId, generation))
        return PresentCompletionAction::IgnoredStale;

    const auto it = findPending(sessionId, generation, completion.id);
    if (it == m_pending.end())
        return PresentCompletionAction::IgnoredUnknown;

    m_pending.erase(it);
    if (isFailureStatus(completion.status))
        return PresentCompletionAction::AcceptedFailure;

    return PresentCompletionAction::AcceptedSuccess;
}

void PresentTracker::clear()
{
    m_pending.clear();
}

std::size_t PresentTracker::pendingCount() const
{
    return m_pending.size();
}

PresentTracker::PendingList::iterator PresentTracker::findPending(
    SessionId sessionId,
    Generation generation,
    PresentId id)
{
    return std::find_if(m_pending.begin(), m_pending.end(), [sessionId, generation, id](const TrackedPresent& present)
    {
        return present.sessionId == sessionId
            && present.generation == generation
            && present.id == id;
    });
}

bool PresentTracker::isCurrent(SessionId sessionId, Generation generation) const
{
    return sessionId == m_sessionId && generation == m_generation;
}

bool PresentTracker::isFailureStatus(PresentStatus status)
{
    return status == PresentStatus::UnsupportedNativeHandle
        || status == PresentStatus::DeviceLost
        || status == PresentStatus::Failed;
}

} // namespace media_sdk::runtime
