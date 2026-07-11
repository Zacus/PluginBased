#include "SessionTimeline.h"

namespace media_sdk::session {

SessionTimeline::SessionTimeline() = default;

SessionTimeline::SessionTimeline(const SessionTimeline& other)
{
    std::lock_guard lock(other.m_mutex);
    m_core = other.m_core;
    m_runtime = other.m_runtime;
    m_accepting = other.m_accepting;
}

SessionTimeline& SessionTimeline::operator=(const SessionTimeline& other)
{
    if (this == &other)
        return *this;

    std::scoped_lock lock(m_mutex, other.m_mutex);
    m_core = other.m_core;
    m_runtime = other.m_runtime;
    m_accepting = other.m_accepting;
    return *this;
}

void SessionTimeline::acceptCoreTimeline(EventMetadata core, runtime::RuntimeTimeline runtime)
{
    std::lock_guard lock(m_mutex);
    m_core = core;
    m_runtime = runtime;
    m_accepting = true;
}

void SessionTimeline::clear()
{
    std::lock_guard lock(m_mutex);
    m_core = {};
    m_runtime = {};
    m_accepting = false;
}

bool SessionTimeline::acceptsCoreEvent(EventMetadata core) const
{
    std::lock_guard lock(m_mutex);
    return acceptsCoreEventLocked(core);
}

std::optional<runtime::RuntimeTimeline> SessionTimeline::runtimeForCoreEvent(EventMetadata core) const
{
    std::lock_guard lock(m_mutex);
    if (!acceptsCoreEventLocked(core))
        return std::nullopt;
    return m_runtime;
}

bool SessionTimeline::acceptsRuntimeTimeline(runtime::RuntimeTimeline runtime) const
{
    std::lock_guard lock(m_mutex);
    return acceptsRuntimeTimelineLocked(runtime);
}

std::optional<EventMetadata> SessionTimeline::coreForRuntimeTimeline(runtime::RuntimeTimeline runtime) const
{
    std::lock_guard lock(m_mutex);
    if (!acceptsRuntimeTimelineLocked(runtime))
        return std::nullopt;
    return m_core;
}

std::optional<EventMetadata> SessionTimeline::coreForRuntimeSession(runtime::SessionId sessionId) const
{
    std::lock_guard lock(m_mutex);
    if (!m_accepting || sessionId != m_runtime.sessionId)
        return std::nullopt;
    return m_core;
}

bool SessionTimeline::hasAcceptedTimeline() const
{
    std::lock_guard lock(m_mutex);
    return m_accepting;
}

bool SessionTimeline::acceptsCoreEventLocked(EventMetadata core) const
{
    return m_accepting
        && core.sessionId == m_core.sessionId
        && core.generation == m_core.generation;
}

bool SessionTimeline::acceptsRuntimeTimelineLocked(runtime::RuntimeTimeline runtime) const
{
    return m_accepting
        && runtime.sessionId == m_runtime.sessionId
        && runtime.generation == m_runtime.generation;
}

} // namespace media_sdk::session
