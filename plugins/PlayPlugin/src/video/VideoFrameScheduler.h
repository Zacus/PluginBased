#pragma once

#include <QtGlobal>

class VideoFrameScheduler
{
public:
    enum class Action {
        Render,
        Wait,
        Drop
    };

    struct Decision {
        Action action = Action::Render;
        qint64 waitUs = 0;
        qint64 diffUs = 0;
    };

    static constexpr qint64 SubmitLeadTimeUs = 2'000;
    static constexpr qint64 LateDropThresholdUs = 100'000;

    static Decision decide(qint64 framePtsUs, qint64 clockUs, bool clockValid);
};
