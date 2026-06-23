#include "video/VideoRenderer.h"
#include "Logger.h"

#include <QPointer>

#if defined(Q_OS_APPLE)
#include <CoreVideo/CoreVideo.h>
#endif

namespace {
constexpr int PerformanceLogIntervalMs = 2000;
constexpr int MaxConsecutiveDropsBeforeRender = 8;
constexpr int QueueEmptyRetryMs = 16;
constexpr int MaxScheduledWaitMs = 40;

NativeVideoFrame nativeVideoFrameFromFrame(const AVFrame* frame)
{
    NativeVideoFrame native;
#if defined(Q_OS_APPLE)
    if (!frame || frame->format != AV_PIX_FMT_VIDEOTOOLBOX)
        return native;

    auto* pixelBuffer = reinterpret_cast<CVPixelBufferRef>(frame->data[3]);
    if (!pixelBuffer)
        return native;

    const OSType cvFormat = CVPixelBufferGetPixelFormatType(pixelBuffer);
    native.kind = NativeFrameKind::VideoToolbox;
    native.pixelFormat = static_cast<int>(cvFormat);
    native.fullRange =
        cvFormat == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange ||
        cvFormat == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange;
    native.is10bit =
        cvFormat == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange ||
        cvFormat == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange;
    native.bt709 =
        frame->colorspace == AVCOL_SPC_BT709 ||
        (frame->colorspace == AVCOL_SPC_UNSPECIFIED && frame->width >= 1280);
#else
    Q_UNUSED(frame);
#endif
    return native;
}
}

VideoRenderer::VideoRenderer(VideoFrameQueue* queue,
                             ClockSync*       clock,
                             QObject*         parent)
    : QObject(parent)
    , m_queue(queue)
    , m_clock(clock)
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &VideoRenderer::onTimer);

    if (m_queue)
    {
        QPointer<VideoRenderer> self(this);
        m_queue->setWakeCallback([self]()
        {
            if (!self)
                return;

            QMetaObject::invokeMethod(self, [self]()
            {
                if (self)
                    self->notifyFrameAvailable();
            }, Qt::QueuedConnection);
        });
    }
}

VideoRenderer::~VideoRenderer()
{
    if (m_queue)
        m_queue->setWakeCallback({});
    stop();
}

void VideoRenderer::start()
{
    m_running = true;
    resetRenderPerformanceStats();
    scheduleImmediateCheck();
    LOG_DEBUG("VideoRenderer: started");
}

void VideoRenderer::stop()
{
    m_running = false;
    cancelScheduledCheck();
    LOG_DEBUG("VideoRenderer: stopped");
}

void VideoRenderer::setPaused(bool paused)
{
    if (m_paused == paused)
        return;

    if (!m_audioClockEnabled && m_videoClock.isValid())
    {
        if (paused)
        {
            m_videoClockPausedUs = m_videoClockBaseUs + m_videoClock.nsecsElapsed() / 1000;
        }
        else if (m_videoClockPausedUs != AV_NOPTS_VALUE)
        {
            m_videoClockBaseUs = m_videoClockPausedUs;
            m_videoClock.restart();
            m_videoClockPausedUs = AV_NOPTS_VALUE;
        }
    }

    m_paused = paused;
    if (m_paused)
        cancelScheduledCheck();
    else
        scheduleImmediateCheck();
}

void VideoRenderer::setAudioClockEnabled(bool enabled)
{
    if (m_audioClockEnabled == enabled)
        return;

    m_audioClockEnabled = enabled;
    resetVideoClock();
    scheduleImmediateCheck();
}

void VideoRenderer::setAcceptedSerial(int serial)
{
    m_acceptedSerial = serial;
    if (m_hasHeld && m_heldEntry.serial != m_acceptedSerial)
    {
        m_hasHeld = false;
        m_heldEntry = {};
        scheduleImmediateCheck();
    }
}

void VideoRenderer::flush()
{
    // seek 时清空，旧帧由 FrameQueue::flush() 已经丢弃
    // 丢弃暂存的旧帧（serial 已过期）
    m_hasHeld = false;
    m_heldEntry = {};
    m_hasRenderedFrame = false;
    m_consecutiveDroppedFrames = 0;
    resetRenderPerformanceStats();
    resetVideoClock();
    cancelScheduledCheck();
}

void VideoRenderer::reset()
{
    m_hasHeld = false;
    m_heldEntry = {};
    m_paused = false;
    m_acceptedSerial = 0;
    m_hasRenderedFrame = false;
    m_consecutiveDroppedFrames = 0;
    resetRenderPerformanceStats();
    resetVideoClock();
    m_pendingSeekGeneration = 0;
    m_seekPending = false;
    m_running = false;
    cancelScheduledCheck();
}

void VideoRenderer::beginSeek(int generation)
{
    m_hasHeld = false;
    m_heldEntry = {};
    m_hasRenderedFrame = false;
    m_consecutiveDroppedFrames = 0;
    resetRenderPerformanceStats();
    setAcceptedSerial(generation);
    m_pendingSeekGeneration = generation;
    m_seekPending = true;
    cancelScheduledCheck();
}

void VideoRenderer::completeSeek(int generation, int serial)
{
    if (!m_seekPending || generation != m_pendingSeekGeneration)
        return;

    setAcceptedSerial(serial);
    m_seekPending = false;
    scheduleImmediateCheck();
}

void VideoRenderer::resetVideoClock()
{
    m_videoClock.invalidate();
    m_videoClockBaseUs = AV_NOPTS_VALUE;
    m_videoClockPausedUs = AV_NOPTS_VALUE;
}

qint64 VideoRenderer::currentVideoClockUs() const
{
    if (m_videoClockBaseUs == AV_NOPTS_VALUE)
        return AV_NOPTS_VALUE;
    if (m_videoClockPausedUs != AV_NOPTS_VALUE)
        return m_videoClockPausedUs;
    return m_videoClockBaseUs + m_videoClock.nsecsElapsed() / 1000;
}

VideoFrameScheduler::Decision VideoRenderer::decideFrame(qint64 framePtsUs)
{
    if (m_audioClockEnabled)
    {
        const qint64 clockUs = m_clock
            ? m_clock->audioClockFast()
            : ClockSync::INVALID_CLOCK;
        return VideoFrameScheduler::decide(framePtsUs,
                                           clockUs,
                                           clockUs != ClockSync::INVALID_CLOCK);
    }

    if (m_videoClockBaseUs == AV_NOPTS_VALUE)
    {
        m_videoClockBaseUs = framePtsUs;
        m_videoClock.restart();
        return {};
    }

    return VideoFrameScheduler::decide(framePtsUs,
                                       currentVideoClockUs(),
                                       true);
}

int VideoRenderer::waitIntervalMs(qint64 waitUs) const
{
    if (waitUs <= 0)
        return 0;

    const qint64 waitMs = (waitUs + 999) / 1000;
    return static_cast<int>(
        qBound<qint64>(1, waitMs, static_cast<qint64>(MaxScheduledWaitMs)));
}

void VideoRenderer::notifyFrameAvailable()
{
    if (m_hasHeld)
        return;

    scheduleImmediateCheck();
}

void VideoRenderer::scheduleNextCheck(int intervalMs)
{
    if (!m_running || m_paused || m_seekPending)
        return;

    m_timer.start(qMax(0, intervalMs));
}

void VideoRenderer::scheduleImmediateCheck()
{
    scheduleNextCheck(0);
}

void VideoRenderer::cancelScheduledCheck()
{
    m_timer.stop();
}

void VideoRenderer::resetRenderPerformanceStats()
{
    m_renderPerf = {};
    m_renderPerfLogTimer.restart();
}

void VideoRenderer::maybeLogRenderPerformance()
{
    if (!m_renderPerfLogTimer.isValid())
        m_renderPerfLogTimer.start();

    const qint64 elapsedMs = m_renderPerfLogTimer.elapsed();
    if (elapsedMs < PerformanceLogIntervalMs)
        return;

    const qint64 totalEvents =
        m_renderPerf.renderedFrames +
        m_renderPerf.droppedFrames +
        m_renderPerf.waitFrames +
        m_renderPerf.queueEmptyWakeups +
        m_renderPerf.staleFrames;
    if (totalEvents <= 0)
    {
        m_renderPerfLogTimer.restart();
        return;
    }

    const double renderFps = elapsedMs > 0
        ? (static_cast<double>(m_renderPerf.renderedFrames) * 1000.0 /
           static_cast<double>(elapsedMs))
        : 0.0;

    LOG_INFO("PlayPerf: renderer rendered={} fps={:.1f} drop={} wait={} empty_wake={} "
             "stale={} forced={} audio_clock={}",
             m_renderPerf.renderedFrames,
             renderFps,
             m_renderPerf.droppedFrames,
             m_renderPerf.waitFrames,
             m_renderPerf.queueEmptyWakeups,
             m_renderPerf.staleFrames,
             m_renderPerf.forcedRenderFrames,
             m_audioClockEnabled);

    m_renderPerf = {};
    m_renderPerfLogTimer.restart();
}

// ─────────────────────────────────────────────────────────────────────────────
// 定时器回调（主线程）
// ─────────────────────────────────────────────────────────────────────────────
void VideoRenderer::onTimer()
{
    processNextFrame();
}

void VideoRenderer::processNextFrame()
{
    if (m_paused)
        return;

    if (m_seekPending)
        return;

    // 一次最多处理有限帧，避免卡死主线程
    for (int attempts = 0; attempts < 4; ++attempts) {

        // 优先消费上次 Wait 时暂存的帧，不再从队列取新帧
        VideoFrameQueue::Entry entry;
        if (m_hasHeld) {
            entry     = std::move(m_heldEntry);
            m_hasHeld = false;
        } else {
            if (!m_queue->tryPop(entry)) {
                ++m_renderPerf.queueEmptyWakeups;
                maybeLogRenderPerformance();
                scheduleNextCheck(QueueEmptyRetryMs);
                return; // 队列空，下次再来
            }
        }

        // EOF 帧
        if (entry.serial != m_acceptedSerial) {
            ++m_renderPerf.staleFrames;
            continue;
        }

        // EOF 帧
        if (entry.eof) {
            LOG_INFO("VideoRenderer: end of video serial={}", entry.serial);
            maybeLogRenderPerformance();
            emit endOfVideo();
            return;
        }

        if (!entry.frame) {
            ++m_renderPerf.staleFrames;
            continue;
        }

        // ── 音视频同步决策 ────────────────────────────────────────────────────
        // frame->pts 已由 media_sdk 统一换算为微秒（AV_TIME_BASE = 1000000）
        qint64 framePtsUs = entry.frame->pts;
        if (framePtsUs == AV_NOPTS_VALUE) framePtsUs = 0;

        const VideoFrameScheduler::Decision decision = decideFrame(framePtsUs);
        const VideoFrameScheduler::Action action = decision.action;

        if (action == VideoFrameScheduler::Action::Wait) {
            // 帧还没到时间：暂存起来，到接近目标显示时间再判断，不丢弃
            m_heldEntry = std::move(entry);
            m_hasHeld   = true;
            ++m_renderPerf.waitFrames;
            maybeLogRenderPerformance();
            scheduleNextCheck(waitIntervalMs(decision.waitUs));
            return;
        }

        if (action == VideoFrameScheduler::Action::Drop &&
            m_hasRenderedFrame &&
            m_consecutiveDroppedFrames < MaxConsecutiveDropsBeforeRender) {
            ++m_consecutiveDroppedFrames;
            ++m_renderPerf.droppedFrames;
            continue; // 丢帧，取下一帧
        }

        if (action == VideoFrameScheduler::Action::Drop)
            ++m_renderPerf.forcedRenderFrames;

        const NativeVideoFrame native = nativeVideoFrameFromFrame(entry.frame.get());
        const bool fullRange = native.isValid()
            ? native.fullRange
            : entry.frame->color_range == AVCOL_RANGE_JPEG;
        const bool bt709 =
            native.isValid()
                ? native.bt709
                : (entry.frame->colorspace == AVCOL_SPC_BT709 ||
                   (entry.frame->colorspace == AVCOL_SPC_UNSPECIFIED &&
                    entry.frame->width >= 1280));
        emit positionChanged(framePtsUs / 1000);
        emit frameReady(make_video_frame(std::move(entry.frame), fullRange, bt709, native));
        m_hasRenderedFrame = true;
        m_consecutiveDroppedFrames = 0;
        ++m_renderPerf.renderedFrames;
        maybeLogRenderPerformance();
        scheduleImmediateCheck();
        return; // 每次定时器只渲染一帧
    }

    scheduleImmediateCheck();
}
