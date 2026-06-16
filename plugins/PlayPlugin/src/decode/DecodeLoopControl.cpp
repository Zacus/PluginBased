#include "decode/DecodeLoopControl.h"

// Implements locked command-state consumption for FFmpegDecoder.
// The returned value can be handled after the mutex is released.

#include <QMutexLocker>

PendingSeekRequest DecodeLoopControl::consumeSeekRequest(QMutex& seekMutex,
                                                         bool& seekRequested,
                                                         qint64& seekTargetMs,
                                                         int& seekGeneration) const
{
    QMutexLocker locker(&seekMutex);
    if (!seekRequested)
        return {};

    PendingSeekRequest request;
    request.requested = true;
    request.targetMs = seekTargetMs;
    request.generation = seekGeneration;
    seekRequested = false;
    return request;
}

EofWaitResult DecodeLoopControl::waitAfterEof(QAtomicInt& stop,
                                              QMutex& seekMutex,
                                              bool& seekRequested,
                                              qint64& seekTargetMs,
                                              int& seekGeneration,
                                              QMutex& openMutex,
                                              QWaitCondition& openCondition,
                                              bool& openRequested) const
{
    while (!stop.loadRelaxed())
    {
        const PendingSeekRequest seekRequest =
            consumeSeekRequest(seekMutex, seekRequested, seekTargetMs, seekGeneration);
        if (seekRequest.requested)
        {
            EofWaitResult result;
            result.decision = EofWaitDecision::SeekRequested;
            result.seek = seekRequest;
            return result;
        }

        QMutexLocker locker(&openMutex);
        if (openRequested)
            return {};

        openCondition.wait(&openMutex, 10);
    }

    return {};
}
