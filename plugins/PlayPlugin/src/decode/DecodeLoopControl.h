#pragma once

// Consumes decode-loop command state guarded by FFmpegDecoder mutexes.
// This helper does not own state; FFmpegDecoder keeps the command fields and signal emission.

#include <QMutex>
#include <QtGlobal>

struct PendingSeekRequest
{
    bool requested = false;
    qint64 targetMs = 0;
    int generation = 0;
};

class DecodeLoopControl
{
public:
    PendingSeekRequest consumeSeekRequest(QMutex& seekMutex,
                                          bool& seekRequested,
                                          qint64& seekTargetMs,
                                          int& seekGeneration) const;
};
