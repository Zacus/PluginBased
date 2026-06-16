#pragma once

// 消费由 FFmpegDecoder 互斥锁保护的解码循环命令状态。
// 该辅助类不拥有状态；命令字段和信号发射仍由 FFmpegDecoder 负责。

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
