#include "playback/PlaybackSeek.h"

#include <algorithm>
#include <limits>

std::optional<qint64> calculateRelativeSeekTarget(
    qint64 currentPositionMs,
    qint64 durationMs,
    qint64 deltaMs)
{
    if (durationMs <= 0 || deltaMs == 0)
        return std::nullopt;

    const qint64 current = std::clamp(currentPositionMs, qint64 { 0 }, durationMs);
    qint64 target = current;
    if (deltaMs > 0) {
        const qint64 remaining = durationMs - current;
        target = deltaMs >= remaining ? durationMs : current + deltaMs;
    } else {
        target = deltaMs <= -current ? 0 : current + deltaMs;
    }

    if (target == current)
        return std::nullopt;
    return target;
}

bool isForwardSeekAvailable(
    bool hasMedia,
    qint64 currentPositionMs,
    qint64 durationMs,
    bool mediaFinished)
{
    return hasMedia
        && durationMs > 0
        && currentPositionMs < durationMs
        && !mediaFinished;
}

int PlaybackSeekState::begin(qint64 targetPositionMs)
{
    if (m_generation == std::numeric_limits<int>::max())
        return 0;
    const int generation = ++m_generation;
    m_pendingGeneration = generation;
    m_pendingPosition = targetPositionMs;
    return generation;
}

qint64 PlaybackSeekState::basePosition(qint64 currentPositionMs) const
{
    return m_pendingPosition.value_or(currentPositionMs);
}

bool PlaybackSeekState::acceptsPositionUpdate() const
{
    return !m_pendingPosition.has_value();
}

bool PlaybackSeekState::isPending(int generation) const
{
    return m_pendingPosition.has_value() && generation == m_pendingGeneration;
}

quint64 PlaybackSeekState::resetVersion() const
{
    return m_resetVersion;
}

bool PlaybackSeekState::complete(int generation)
{
    if (!m_pendingPosition || generation != m_pendingGeneration)
        return false;

    m_pendingGeneration = 0;
    m_pendingPosition.reset();
    return true;
}

void PlaybackSeekState::reset()
{
    ++m_resetVersion;
    m_pendingGeneration = 0;
    m_pendingPosition.reset();
}
