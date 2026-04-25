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
    m_swsCtx.reset();
    LOG_DEBUG("VideoRenderer: stopped");
}

void VideoRenderer::flush()
{
    // seek 时清空，旧帧由 FrameQueue::flush() 已经丢弃
    ++m_currentSerial;
    // 丢弃暂存的旧帧（serial 已过期）
    m_hasHeld = false;
    m_heldEntry = {};
}

void VideoRenderer::setSourceSize(int width, int height, AVPixelFormat fmt)
{
    if (m_srcWidth == width && m_srcHeight == height && m_srcFmt == fmt)
        return;
    m_srcWidth  = width;
    m_srcHeight = height;
    m_srcFmt    = fmt;
    m_swsCtx.reset(); // 触发重建
    LOG_INFO("VideoRenderer: source size {}x{} fmt={}", width, height,
             av_get_pix_fmt_name(fmt));
}

// ─────────────────────────────────────────────────────────────────────────────
// 定时器回调（主线程）
// ─────────────────────────────────────────────────────────────────────────────
void VideoRenderer::onTimer()
{
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

        // 丢弃 seek 前的旧帧（serial 不匹配）
        if (entry.serial < m_currentSerial) continue;

        // ── 音视频同步决策 ────────────────────────────────────────────────────
        // frame->pts 已由 FFmpegDecoder 统一换算为微秒（AV_TIME_BASE = 1000000）
        qint64 framePtsUs = entry.frame->pts;
        if (framePtsUs == AV_NOPTS_VALUE) framePtsUs = 0;

        const ClockSync::Action action = m_clock->decide(framePtsUs);

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

        // ── 渲染 ──────────────────────────────────────────────────────────────
        if (!m_swsCtx) {
            if (!initSwsContext()) return;
        }

        QImage img = convertFrame(entry.frame.get());
        if (!img.isNull()) {
            emit frameReady(img);
        }
        return; // 每次定时器只渲染一帧
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 初始化 swscale
// ─────────────────────────────────────────────────────────────────────────────
bool VideoRenderer::initSwsContext()
{
    if (m_srcWidth <= 0 || m_srcHeight <= 0) return false;

    SwsContext* sws = sws_getContext(
        m_srcWidth, m_srcHeight, m_srcFmt,           // 源
        m_srcWidth, m_srcHeight, AV_PIX_FMT_RGB32,   // 目标：RGB32 = BGRA（Qt 兼容）
        SWS_BILINEAR,
        nullptr, nullptr, nullptr
    );

    if (!sws) {
        LOG_ERROR("VideoRenderer: sws_getContext failed");
        return false;
    }

    m_swsCtx.reset(sws);

    // 预分配输出图像（避免每帧 malloc）
    m_outputImage = QImage(m_srcWidth, m_srcHeight, QImage::Format_ARGB32);
    LOG_DEBUG("VideoRenderer: swscale initialized {}x{}", m_srcWidth, m_srcHeight);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 帧转换 YUV → QImage
// ─────────────────────────────────────────────────────────────────────────────
QImage VideoRenderer::convertFrame(AVFrame* frame)
{
    if (!m_swsCtx || m_outputImage.isNull()) return {};

    // sws_scale 直接写入 QImage 的内存，零额外拷贝
    uint8_t* dstData[1]     = { m_outputImage.bits() };
    int      dstLinesize[1] = { static_cast<int>(m_outputImage.bytesPerLine()) };

    const int ret = sws_scale(
        m_swsCtx.get(),
        frame->data, frame->linesize,          // 源
        0, frame->height,                      // 源行范围
        dstData, dstLinesize                   // 目标
    );

    if (ret <= 0) {
        LOG_WARN("VideoRenderer: sws_scale returned {}", ret);
        return {};
    }

    // 返回 QImage 的浅拷贝（共享数据，copy-on-write，效率高）
    return m_outputImage;
}
