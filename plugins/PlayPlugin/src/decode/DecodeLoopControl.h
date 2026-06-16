#pragma once

// Consumes decode-loop command state guarded by FFmpegDecoder mutexes.
// This helper does not own state; FFmpegDecoder keeps the command fields and signal emission.

#include <QMutex>
#include <QAtomicInt>
#include <QWaitCondition>
#include <QtGlobal>

struct PendingSeekRequest
{
    bool requested = false;
    qint64 targetMs = 0;
    int generation = 0;
};

enum class EofWaitDecision
{
    StopOrNewOpen,
    SeekRequested
};

struct EofWaitResult
{
    EofWaitDecision decision = EofWaitDecision::StopOrNewOpen;
    PendingSeekRequest seek;
};

class DecodeLoopControl
{
public:
    PendingSeekRequest consumeSeekRequest(QMutex& seekMutex,
                                          bool& seekRequested,
                                          qint64& seekTargetMs,
                                          int& seekGeneration) const;

    EofWaitResult waitAfterEof(QAtomicInt& stop,
                               QMutex& seekMutex,
                               bool& seekRequested,
                               qint64& seekTargetMs,
                               int& seekGeneration,
                               QMutex& openMutex,
                               QWaitCondition& openCondition,
                               bool& openRequested) const;
};
