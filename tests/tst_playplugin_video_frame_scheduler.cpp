#include "video/VideoFrameScheduler.h"

#include <QTest>

class PlayPluginVideoFrameSchedulerTest : public QObject
{
    Q_OBJECT

private slots:
    void invalidClockRendersImmediately()
    {
        const auto decision = VideoFrameScheduler::decide(1'000'000,
                                                          0,
                                                          false);

        QCOMPARE(decision.action, VideoFrameScheduler::Action::Render);
        QCOMPARE(decision.waitUs, 0);
    }

    void earlyFrameReportsPreciseWaitTime()
    {
        const auto decision = VideoFrameScheduler::decide(1'050'000,
                                                          1'000'000,
                                                          true);

        QCOMPARE(decision.action, VideoFrameScheduler::Action::Wait);
        QCOMPARE(decision.waitUs, 48'000);
        QCOMPARE(decision.diffUs, 50'000);
    }

    void frameInsideSubmitLeadRenders()
    {
        const auto decision = VideoFrameScheduler::decide(1'001'500,
                                                          1'000'000,
                                                          true);

        QCOMPARE(decision.action, VideoFrameScheduler::Action::Render);
        QCOMPARE(decision.waitUs, 0);
    }

    void veryLateFrameDrops()
    {
        const auto decision = VideoFrameScheduler::decide(900'000,
                                                          1'001'000,
                                                          true);

        QCOMPARE(decision.action, VideoFrameScheduler::Action::Drop);
        QCOMPARE(decision.waitUs, 0);
    }
};

QTEST_MAIN(PlayPluginVideoFrameSchedulerTest)
#include "tst_playplugin_video_frame_scheduler.moc"
