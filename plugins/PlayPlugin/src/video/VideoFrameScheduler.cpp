#include "video/VideoFrameScheduler.h"

VideoFrameScheduler::Decision VideoFrameScheduler::decide(qint64 framePtsUs,
                                                          qint64 clockUs,
                                                          bool clockValid)
{
    if (!clockValid)
        return {};

    const qint64 diffUs = framePtsUs - clockUs;
    if (diffUs > SubmitLeadTimeUs)
        return { Action::Wait, diffUs - SubmitLeadTimeUs, diffUs };

    if (diffUs < -LateDropThresholdUs)
        return { Action::Drop, 0, diffUs };

    return { Action::Render, 0, diffUs };
}
