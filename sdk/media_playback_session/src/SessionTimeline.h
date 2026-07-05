#pragma once

#include "media_sdk/MediaEvents.h"
#include "media_sdk/runtime/RuntimeTypes.h"

#include <mutex>
#include <optional>

namespace media_sdk::session {

class SessionTimeline
{
public:
    SessionTimeline() = default;

    SessionTimeline(const SessionTimeline& other)
    {
        std::lock_guard lock(other.m_mutex);
        m_core = other.m_core;
        m_runtime = other.m_runtime;
        m_accepting = other.m_accepting;
    }

    SessionTimeline& operator=(const SessionTimeline& other)
    {
        if (this == &other)
            return *this;

        std::scoped_lock lock(m_mutex, other.m_mutex);
        m_core = other.m_core;
        m_runtime = other.m_runtime;
        m_accepting = other.m_accepting;
        return *this;
    }

    void acceptCoreTimeline(EventMetadata core, runtime::RuntimeTimeline runtime)
    {
        std::lock_guard lock(m_mutex);
        m_core = core;
        m_runtime = runtime;
        m_accepting = true;
    }

    void clear()
    {
        std::lock_guard lock(m_mutex);
        m_core = {};
        m_runtime = {};
        m_accepting = false;
    }

    [[nodiscard("false means the core event belongs to a stale or unopened timeline")]]
    bool acceptsCoreEvent(EventMetadata core) const
    {
        std::lock_guard lock(m_mutex);
        return acceptsCoreEventLocked(core);
    }

    [[nodiscard("empty means the core event belongs to a stale or unopened timeline")]]
    std::optional<runtime::RuntimeTimeline> runtimeForCoreEvent(EventMetadata core) const
    {
        std::lock_guard lock(m_mutex);
        if (!acceptsCoreEventLocked(core))
            return std::nullopt;
        return m_runtime;
    }

    [[nodiscard("false means the runtime callback belongs to a stale or replaced timeline")]]
    bool acceptsRuntimeTimeline(runtime::RuntimeTimeline runtime) const
    {
        std::lock_guard lock(m_mutex);
        return acceptsRuntimeTimelineLocked(runtime);
    }

    [[nodiscard("empty means the runtime callback belongs to a stale or replaced timeline")]]
    std::optional<EventMetadata> coreForRuntimeTimeline(runtime::RuntimeTimeline runtime) const
    {
        std::lock_guard lock(m_mutex);
        if (!acceptsRuntimeTimelineLocked(runtime))
            return std::nullopt;
        return m_core;
    }

    [[nodiscard("empty means the runtime session is stale or no core timeline is active")]]
    std::optional<EventMetadata> coreForRuntimeSession(runtime::SessionId sessionId) const
    {
        std::lock_guard lock(m_mutex);
        if (!m_accepting || sessionId != m_runtime.sessionId)
            return std::nullopt;
        return m_core;
    }

    [[nodiscard("false means no core/runtime timeline is currently accepting frames or events")]]
    bool hasAcceptedTimeline() const
    {
        std::lock_guard lock(m_mutex);
        return m_accepting;
    }

private:
    bool acceptsCoreEventLocked(EventMetadata core) const
    {
        return m_accepting
            && core.sessionId == m_core.sessionId
            && core.generation == m_core.generation;
    }

    bool acceptsRuntimeTimelineLocked(runtime::RuntimeTimeline runtime) const
    {
        return m_accepting
            && runtime.sessionId == m_runtime.sessionId
            && runtime.generation == m_runtime.generation;
    }

    mutable std::mutex m_mutex;
    EventMetadata m_core {};
    runtime::RuntimeTimeline m_runtime {};
    bool m_accepting = false;
};

} // namespace media_sdk::session
