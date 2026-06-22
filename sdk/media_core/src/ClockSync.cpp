#include "ClockSync.h"

namespace media_sdk {

ClockSync::ClockSync() = default;

void ClockSync::setAudioClock(std::chrono::microseconds position)
{
    std::scoped_lock lock(m_mutex);
    m_snapshot.base = position;
    m_snapshot.anchor = Clock::now();
    m_snapshot.valid = true;
    m_snapshot.paused = false;
}

void ClockSync::invalidate()
{
    std::scoped_lock lock(m_mutex);
    m_snapshot = {};
}

void ClockSync::setPaused(bool paused)
{
    std::scoped_lock lock(m_mutex);
    if (!m_snapshot.valid || m_snapshot.paused == paused)
        return;

    if (paused)
    {
        m_snapshot.base += std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - m_snapshot.anchor);
    }
    else
    {
        m_snapshot.anchor = Clock::now();
    }

    m_snapshot.paused = paused;
}

std::optional<std::chrono::microseconds> ClockSync::currentTime() const
{
    std::scoped_lock lock(m_mutex);
    if (!m_snapshot.valid)
        return std::nullopt;
    if (m_snapshot.paused)
        return m_snapshot.base;

    return m_snapshot.base +
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - m_snapshot.anchor);
}

} // namespace media_sdk
