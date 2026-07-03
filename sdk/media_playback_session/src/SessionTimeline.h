#pragma once

#include "media_sdk/MediaEvents.h"
#include "media_sdk/runtime/RuntimeTypes.h"

#include <optional>

namespace media_sdk::session {

class SessionTimeline
{
public:
    void acceptCoreTimeline(EventMetadata core, runtime::RuntimeTimeline runtime)
    {
        m_core = core;
        m_runtime = runtime;
        m_accepting = true;
    }

    void clear()
    {
        m_core = {};
        m_runtime = {};
        m_accepting = false;
    }

    [[nodiscard("false means the core event belongs to a stale or unopened timeline")]]
    bool acceptsCoreEvent(EventMetadata core) const
    {
        return m_accepting
            && core.sessionId == m_core.sessionId
            && core.generation == m_core.generation;
    }

    [[nodiscard("empty means the core event belongs to a stale or unopened timeline")]]
    std::optional<runtime::RuntimeTimeline> runtimeForCoreEvent(EventMetadata core) const
    {
        if (!acceptsCoreEvent(core))
            return std::nullopt;
        return m_runtime;
    }

    [[nodiscard("false means the runtime callback belongs to a stale or replaced timeline")]]
    bool acceptsRuntimeTimeline(runtime::RuntimeTimeline runtime) const
    {
        return m_accepting
            && runtime.sessionId == m_runtime.sessionId
            && runtime.generation == m_runtime.generation;
    }

private:
    EventMetadata m_core {};
    runtime::RuntimeTimeline m_runtime {};
    bool m_accepting = false;
};

} // namespace media_sdk::session
