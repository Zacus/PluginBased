#include "VideoRenderer.h"
#include "Logger.h"

VideoRenderer::VideoRenderer(VideoFrameQueue* queue,
                             ClockSync*       clock,
                             QObject*         parent)
    : QObject(parent)
    , m_queue(queue)
    , m_clock(clock)
{
    m_timer.setInterval(8); // ~120fps 上限，实际受音频时钟同步限制
    connect(&m_timer, &QTimer::timeout, this, &VideoRenderer::onTimer);
}

VideoRenderer::~VideoRenderer()
{
    stop();
}

void VideoRenderer::start()
{
    m_timer.start();
    LOG_DEBUG("VideoRenderer: started");
}

void VideoRenderer::stop()
{
    m_timer.stop();
    LOG_DEBUG("VideoRenderer: stopped");
}

void VideoRenderer::flush()
{
    // seek 时清空，旧帧由 FrameQueue::flush() 已经丢弃
    // 丢弃暂存的旧帧（serial 已过期）
    m_hasHeld = false;
    m_heldEntry = {};
}

void VideoRenderer::reset()
{
    m_hasHeld = false;
    m_heldEntry = {};
    m_pendingSeekGeneration = 0;
    m_seekPending = false;
}

void VideoRenderer::beginSeek(int generation)
{
    m_hasHeld = false;
    m_heldEntry = {};
    m_pendingSeekGeneration = generation;
    m_seekPending = true;
}

void VideoRenderer::completeSeek(int generation)
{
    if (!m_seekPending || generation != m_pendingSeekGeneration)
        return;

    m_seekPending = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// 定时器回调（主线程）
// ─────────────────────────────────────────────────────────────────────────────
void VideoRenderer::onTimer()
{
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
            if (!m_queue->tryPop(entry)) return; // 队列空，下次再来
        }

        // EOF 帧
        if (entry.eof) {
            emit endOfVideo();
            return;
        }

        if (!entry.frame) continue;

        // ── 音视频同步决策 ────────────────────────────────────────────────────
        // frame->pts 已由 FFmpegDecoder 统一换算为微秒（AV_TIME_BASE = 1000000）
        qint64 framePtsUs = entry.frame->pts;
        if (framePtsUs == AV_NOPTS_VALUE) framePtsUs = 0;

        const ClockSync::Action action = m_clock->decide(framePtsUs);

                // ← 加这行，每次决策都打出来
        LOG_DEBUG("VideoRenderer: pts={}µs clock={}µs action={}",
                  framePtsUs, m_clock->audioClock(),
                  action == ClockSync::Action::Wait   ? "Wait" :
                  action == ClockSync::Action::Drop   ? "Drop" : "Render");

        if (action == ClockSync::Action::Wait) {
            // 帧还没到时间：暂存起来，8ms 后再判断，不丢弃
            m_heldEntry = std::move(entry);
            m_hasHeld   = true;
            return;
        }

        if (action == ClockSync::Action::Drop) {
            LOG_DEBUG("VideoRenderer: drop frame pts={}", framePtsUs);
            continue; // 丢帧，取下一帧
        }

        const bool fullRange = entry.frame->color_range == AVCOL_RANGE_JPEG;
        const bool bt709 =
            entry.frame->colorspace == AVCOL_SPC_BT709 ||
            (entry.frame->colorspace == AVCOL_SPC_UNSPECIFIED &&
             entry.frame->width >= 1280);
        emit frameReady(make_video_frame(std::move(entry.frame), fullRange, bt709));
        return; // 每次定时器只渲染一帧
    }
}
