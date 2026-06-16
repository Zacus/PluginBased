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
