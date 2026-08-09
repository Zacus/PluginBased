#pragma once

#include <QtGlobal>

#include <optional>

[[nodiscard]] std::optional<qint64> calculateRelativeSeekTarget(
    qint64 currentPositionMs,
    qint64 durationMs,
    qint64 deltaMs);

[[nodiscard]] bool isForwardSeekAvailable(
    bool hasMedia,
    qint64 currentPositionMs,
    qint64 durationMs,
    bool mediaFinished);

class PlaybackSeekState
{
public:
    int begin(qint64 targetPositionMs);
    [[nodiscard]] qint64 basePosition(qint64 currentPositionMs) const;
    [[nodiscard]] bool acceptsPositionUpdate() const;
    [[nodiscard]] bool isPending(int generation) const;
    [[nodiscard]] quint64 resetVersion() const;
    [[nodiscard]] bool complete(int generation);
    void reset();

private:
    int m_generation = 0;
    int m_pendingGeneration = 0;
    quint64 m_resetVersion = 0;
    std::optional<qint64> m_pendingPosition;
};
