#pragma once

#include "media_sdk/MediaEvents.h"
#include "media_sdk/runtime/RuntimeTypes.h"

#include <mutex>
#include <optional>

namespace media_sdk::session {

class SessionTimeline
{
public:
    SessionTimeline();
    SessionTimeline(const SessionTimeline& other);
    SessionTimeline& operator=(const SessionTimeline& other);

    void acceptCoreTimeline(EventMetadata core, runtime::RuntimeTimeline runtime);
    void clear();

    [[nodiscard("false means the core event belongs to a stale or unopened timeline")]]
    bool acceptsCoreEvent(EventMetadata core) const;

    [[nodiscard("empty means the core event belongs to a stale or unopened timeline")]]
    std::optional<runtime::RuntimeTimeline> runtimeForCoreEvent(EventMetadata core) const;

    [[nodiscard("false means the runtime callback belongs to a stale or replaced timeline")]]
    bool acceptsRuntimeTimeline(runtime::RuntimeTimeline runtime) const;

    [[nodiscard("empty means the runtime callback belongs to a stale or replaced timeline")]]
    std::optional<EventMetadata> coreForRuntimeTimeline(runtime::RuntimeTimeline runtime) const;

    [[nodiscard("empty means the runtime session is stale or no core timeline is active")]]
    std::optional<EventMetadata> coreForRuntimeSession(runtime::SessionId sessionId) const;

    [[nodiscard("false means no core/runtime timeline is currently accepting frames or events")]]
    bool hasAcceptedTimeline() const;

private:
    bool acceptsCoreEventLocked(EventMetadata core) const;
    bool acceptsRuntimeTimelineLocked(runtime::RuntimeTimeline runtime) const;

    mutable std::mutex m_mutex;
    EventMetadata m_core {};
    runtime::RuntimeTimeline m_runtime {};
    bool m_accepting = false;
};

} // namespace media_sdk::session
