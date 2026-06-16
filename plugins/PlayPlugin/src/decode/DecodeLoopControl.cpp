#include "decode/DecodeLoopControl.h"

// 实现 FFmpegDecoder 解码循环中受锁保护的命令状态消费。
// 返回值可在释放互斥锁后继续处理，减少锁内工作量。

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
