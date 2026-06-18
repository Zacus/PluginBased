#pragma once

#include "common/FrameQueue.h"
#include "sync/ClockSync.h"
#include "common/FFmpegUtils.h"

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>

/**
 * @brief VideoRenderer — 视频帧渲染器（主线程）
 *
 * 用 QTimer（默认 8ms，约 120fps 上限）驱动，运行在主线程。
 * 从 VideoFrameQueue 取帧，与音频时钟同步后，把选中的 AVFrame
 * 直接转交给 FFmpegSurface，由 Scene Graph / shader 完成 YUV 上屏。
 *
 * 同步策略（以音频时钟为主）：
 *   - 帧 PTS 比音频时钟早 > 40ms → 不取帧，等下次 timer 触发
 *   - 帧 PTS 比音频时钟晚 > 100ms → 丢帧，取下一帧
 *   - 否则渲染
 *
 * 运行在主线程的理由：
 *   QSGNode 更新只能在 Qt 渲染线程或主线程（通过 update()）触发，
 *   用 QTimer 在主线程取帧是最简单可靠的方式，避免跨线程操作 QQuickItem。
 */
class VideoRenderer : public QObject
{
    Q_OBJECT

public:
    explicit VideoRenderer(VideoFrameQueue* queue,
                           ClockSync*       clock,
                           QObject*         parent = nullptr);
    ~VideoRenderer() override;

    void start();                // 开始渲染定时器
    void stop();                 // 停止渲染定时器
    void setPaused(bool paused); // 暂停/恢复渲染推进
    void setAudioClockEnabled(bool enabled);
    void setAcceptedSerial(int serial);
    void flush();                // seek 时调用
    void reset();                // 新媒体/停止时重置跨文件状态
    void beginSeek(int generation);
    void completeSeek(int generation, int serial);

signals:
    /** 每一帧待显示的视频帧，发给 FFmpegSurface */
    void frameReady(const VideoFrameDataPtr& frame);

    /** 视频流 EOF */
    void endOfVideo();

    /** 实际上屏的视频帧位置 */
    void positionChanged(qint64 posMs);

private slots:
    void onTimer();

private:
    struct VideoRenderPerformanceStats {
        qint64 renderedFrames = 0;
        qint64 droppedFrames = 0;
        qint64 waitFrames = 0;
        qint64 queueEmptyPolls = 0;
        qint64 staleFrames = 0;
        qint64 forcedRenderFrames = 0;
    };

    VideoFrameQueue* m_queue = nullptr;
    ClockSync*       m_clock = nullptr;

    QTimer  m_timer;

    // 上次 Wait 时暂存的帧（下次 timer 优先消费）
    VideoFrameQueue::Entry m_heldEntry;
    bool                   m_hasHeld = false;

    QElapsedTimer m_videoClock;
    qint64 m_videoClockBaseUs = AV_NOPTS_VALUE;
    qint64 m_videoClockPausedUs = AV_NOPTS_VALUE;
    bool   m_audioClockEnabled = true;
    bool   m_paused = false;
    int    m_acceptedSerial = 0;
    bool   m_hasRenderedFrame = false;
    int    m_consecutiveDroppedFrames = 0;
    VideoRenderPerformanceStats m_renderPerf;
    QElapsedTimer m_renderPerfLogTimer;

    int  m_pendingSeekGeneration = 0;
    bool m_seekPending = false;

    void resetVideoClock();
    ClockSync::Action decideVideoOnly(qint64 framePtsUs);
    void resetRenderPerformanceStats();
    void maybeLogRenderPerformance();
};
