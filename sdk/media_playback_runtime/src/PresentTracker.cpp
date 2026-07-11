#include "PresentTracker.h"

#include <algorithm>

namespace media_sdk::runtime {

void PresentTracker::reset(SessionId sessionId, Generation generation)
{
    m_sessionId = sessionId;
    m_generation = generation;
    m_pending.clear();
    m_earlyCompletions.clear();
}

void PresentTracker::setMaxPending(std::size_t maxPending)
{
    m_maxPending = maxPending == 0 ? 1 : maxPending;
}

bool PresentTracker::track(TrackedPresent present)
{
    return trackResult(present).action != PresentTrackAction::Rejected;
}

PresentTrackResult PresentTracker::trackResult(TrackedPresent present)
{
    if (!isCurrent(present.sessionId, present.generation) || present.id == 0)
        return {};

    if (findPending(present.sessionId, present.generation, present.id) != m_pending.end())
        return {};

    const auto earlyCompletion = findEarlyCompletion(present.id);
    if (earlyCompletion != m_earlyCompletions.end()) {
        const auto status = earlyCompletion->status;
        const auto diagnostics = earlyCompletion->diagnostics;
        m_earlyCompletions.erase(earlyCompletion);
        return {
            .action = isFailureStatus(status)
                ? PresentTrackAction::ConsumedFailure
                : PresentTrackAction::ConsumedSuccess,
            .completionStatus = status,
            .diagnostics = diagnostics,
        };
    }

    if (!hasCapacity())
        return {};

    m_pending.push_back(present);
    return {
        .action = PresentTrackAction::TrackedPending,
    };
}

PresentCompletionAction PresentTracker::complete(
    SessionId sessionId,
    Generation generation,
    PresentCompletion completion)
{
    if (!isCurrent(sessionId, generation))
        return PresentCompletionAction::IgnoredStale;

    const auto it = findPending(sessionId, generation, completion.id);
    if (it == m_pending.end()) {
        // 异步 presenter 可能在 present() 返回后、runtime track() 前完成。
        // 暂存当前 generation 的 completion，避免后续 track 留下无法释放的 pending present。
        if (completion.id != 0) {
            constexpr std::size_t kMaxEarlyCompletions = 8;
            if (m_earlyCompletions.size() >= kMaxEarlyCompletions)
                m_earlyCompletions.pop_front();
            m_earlyCompletions.push_back(std::move(completion));
        }
        return PresentCompletionAction::IgnoredUnknown;
    }

    m_pending.erase(it);
    if (isFailureStatus(completion.status))
        return PresentCompletionAction::AcceptedFailure;

    return PresentCompletionAction::AcceptedSuccess;
}

void PresentTracker::clear()
{
    m_pending.clear();
    m_earlyCompletions.clear();
}

bool PresentTracker::hasCapacity() const
{
    return m_pending.size() < m_maxPending;
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

PresentTracker::CompletionList::iterator PresentTracker::findEarlyCompletion(PresentId id)
{
    return std::find_if(m_earlyCompletions.begin(), m_earlyCompletions.end(), [id](const PresentCompletion& completion)
    {
        return completion.id == id;
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
