#include "FFmpegDecoder.h"
#include "Logger.h"

// ─────────────────────────────────────────────────────────────────────────────
// 构造 / 析构
// ─────────────────────────────────────────────────────────────────────────────
FFmpegDecoder::FFmpegDecoder(VideoFrameQueue* videoQ, AudioFrameQueue* audioQ, QObject* parent)
    : QThread(parent), m_videoQueue(videoQ), m_audioQueue(audioQ)
{
    // FFmpeg 日志重定向到 spdlog
    av_log_set_callback(
        [](void*, int level, const char* fmt, va_list args)
        {
            if (level > AV_LOG_WARNING)
                return;
            char buf[1024];
            vsnprintf(buf, sizeof(buf), fmt, args);
            // 去掉末尾换行
            std::string s(buf);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
                s.pop_back();
            if (s.empty())
                return;
            if (level <= AV_LOG_ERROR)
                LOG_ERROR("[FFmpeg] {}", s);
            else
                LOG_WARN("[FFmpeg] {}", s);
        });
}

FFmpegDecoder::~FFmpegDecoder()
{
    stopDecoding();
    wait(); // 等待线程退出
}

// ─────────────────────────────────────────────────────────────────────────────
// 主线程接口（线程安全）
// ─────────────────────────────────────────────────────────────────────────────
void FFmpegDecoder::openFile(const QUrl& url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();

    {
        QMutexLocker lk(&m_openMutex);
        m_pendingPath = path;
        m_openRequested = true;
        m_stop.storeRelaxed(0);
        m_paused.storeRelaxed(0);
    }

    // 若线程还未启动则启动，否则唤醒
    if (!isRunning())
    {
        start(QThread::HighPriority);
    }
    else
    {
        m_openCond.wakeOne();
    }
}

void FFmpegDecoder::seekTo(qint64 posMs, int generation)
{
    {
        QMutexLocker lk(&m_seekMutex);
        m_seekRequested = true;
        m_seekTargetMs = posMs;
        m_seekGeneration = generation;
    }
    // 如果处于暂停状态，也需要执行 seek
    m_seekCond.wakeOne();
    m_openCond.wakeOne();
}

void FFmpegDecoder::setPaused(bool paused)
{
    m_paused.storeRelaxed(paused ? 1 : 0);
    if (!paused)
        m_seekCond.wakeOne(); // 恢复时唤醒暂停等待
}

void FFmpegDecoder::stopDecoding()
{
    m_stop.storeRelaxed(1);
    m_paused.storeRelaxed(0);

    // 清空队列，唤醒所有阻塞的 push/pop
    if (m_videoQueue)
        m_videoQueue->abort();
    if (m_audioQueue)
        m_audioQueue->abort();

    // 唤醒可能阻塞在等待 open/seek 的地方
    m_openCond.wakeAll();
    m_seekCond.wakeAll();
}

void FFmpegDecoder::setVideoToolboxDirectRenderingEnabled(bool enabled)
{
    m_videoToolboxDirectRenderingEnabled.storeRelaxed(enabled ? 1 : 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 线程主函数
// ─────────────────────────────────────────────────────────────────────────────
void FFmpegDecoder::run()
{
    LOG_INFO("FFmpegDecoder thread started");

    while (!m_stop.loadRelaxed())
    {
        // 等待 open 请求
        QString path;
        {
            QMutexLocker lk(&m_openMutex);
            while (!m_openRequested && !m_stop.loadRelaxed())
                m_openCond.wait(&m_openMutex);
            if (m_stop.loadRelaxed())
                break;
            path = m_pendingPath;
            m_openRequested = false;
        }

        // 重置队列（新文件，丢弃之前的帧）
        m_flushSerial = 0;
        m_videoQueue->resetAbort();
        m_audioQueue->resetAbort();
        m_videoQueue->flush();
        m_audioQueue->flush();

        if (!openInternal(path))
        {
            // 错误已通过 errorOccurred 信号发出
            closeInternal();
            continue;
        }

        // 发送媒体信息
        emit mediaInfoReady(m_durationMs, m_videoWidth, m_videoHeight, m_videoFps,
                            m_audioSampleRate, m_audioChannels, m_audioChannelLayoutMask,
                            m_audioSampleFmt, m_formatName);

        m_decodePerf.reset();
        decodeLoop();
        closeInternal();
    }

    LOG_INFO("FFmpegDecoder thread exiting");
}

// ─────────────────────────────────────────────────────────────────────────────
// 打开文件
// ─────────────────────────────────────────────────────────────────────────────
bool FFmpegDecoder::openInternal(const QString& path)
{
    MediaOpenResult result = m_mediaOpener.open(path);
    if (!result.ok)
    {
        emit errorOccurred(result.errorMessage);
        return false;
    }

    OpenedMedia& media = result.media;
    m_fmtCtx = std::move(media.formatContext);
    m_videoCodecCtx = std::move(media.videoCodecContext);
    m_audioCodecCtx = std::move(media.audioCodecContext);
    m_hardwareDecoder = std::move(media.hardwareDecoder);
    m_videoStreamIdx = media.videoStreamIndex;
    m_audioStreamIdx = media.audioStreamIndex;
    m_durationMs = media.durationMs;
    m_videoWidth = media.videoWidth;
    m_videoHeight = media.videoHeight;
    m_videoFps = media.videoFps;
    m_audioChannels = media.audioChannels;
    m_audioSampleRate = media.audioSampleRate;
    m_audioChannelLayoutMask = media.audioChannelLayoutMask;
    m_audioSampleFmt = media.audioSampleFormat;
    m_formatName = media.formatName;
    m_activeVideoDecoderName = media.activeVideoDecoderName;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 解码主循环
// ─────────────────────────────────────────────────────────────────────────────
void FFmpegDecoder::decodeLoop()
{
    auto pkt = make_packet();

    while (!m_stop.loadRelaxed())
    {

        // ── 暂停等待 ──────────────────────────────────────────────────────────
        if (m_paused.loadRelaxed())
        {
            QMutexLocker lk(&m_seekMutex);
            // 暂停期间仍响应 seek 请求
            if (!m_seekRequested)
                m_seekCond.wait(&m_seekMutex, 10);
        }

        // ── seek 处理 ─────────────────────────────────────────────────────────
        {
            QMutexLocker lk(&m_seekMutex);
            if (m_seekRequested)
            {
                const qint64 seekTargetMs = m_seekTargetMs;
                const int seekGeneration = m_seekGeneration;
                m_seekRequested = false;
                lk.unlock();
                doSeek(seekTargetMs, seekGeneration);
                emit seekCompleted(seekGeneration, m_flushSerial);
                continue;
            }
        }

        // ── 读取下一个 packet ─────────────────────────────────────────────────
        int ret = av_read_frame(m_fmtCtx.get(), pkt.get());
        if (ret == AVERROR_EOF)
        {
            LOG_INFO("EOF at fmtCtx duration={} seekTarget={}", m_fmtCtx->duration / AV_TIME_BASE,
                     m_seekTargetMs / 1000.0);
            // 文件结束：向两个队列各推一个 eof 标记帧
            if (m_videoStreamIdx >= 0)
                m_videoQueue->push(nullptr, m_flushSerial, true);
            if (m_audioStreamIdx >= 0)
                m_audioQueue->push(nullptr, m_flushSerial, true);
            emit endOfFile();

            // 等待 stop、新 open，或结束后从 UI 发起的 seek。
            bool openRequested = false;
            while (!m_stop.loadRelaxed())
            {
                {
                    QMutexLocker lk(&m_seekMutex);
                    if (m_seekRequested)
                    {
                        const qint64 seekTargetMs = m_seekTargetMs;
                        const int seekGeneration = m_seekGeneration;
                        m_seekRequested = false;
                        lk.unlock();
                        doSeek(seekTargetMs, seekGeneration);
                        emit seekCompleted(seekGeneration, m_flushSerial);
                        break;
                    }
                }

                {
                    QMutexLocker lk(&m_openMutex);
                    if (m_openRequested)
                    {
                        openRequested = true;
                        break;
                    }
                    m_openCond.wait(&m_openMutex, 10);
                }
            }

            if (m_stop.loadRelaxed() || openRequested)
                break; // 退出本次 decodeLoop，回到 run() 的外层循环处理新文件
            continue;
        }
        if (ret < 0)
        {
            LOG_WARN("FFmpegDecoder: av_read_frame error: {}", av_err(ret));
            break;
        }

        // ── 分发 packet 到对应解码器 ──────────────────────────────────────────
        if (pkt->stream_index == m_videoStreamIdx)
        {
            sendPacketToDecoder(m_videoCodecCtx.get(), pkt.get(), m_videoQueue, m_flushSerial);
        }
        else if (pkt->stream_index == m_audioStreamIdx)
        {
            sendPacketToDecoder(m_audioCodecCtx.get(), pkt.get(), m_audioQueue, m_flushSerial);
        }

        av_packet_unref(pkt.get());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 发 packet 给解码器，收取所有解码帧写入队列
// ─────────────────────────────────────────────────────────────────────────────
bool FFmpegDecoder::sendPacketToDecoder(AVCodecContext* ctx, AVPacket* pkt,
                                        FrameQueue<AVFramePtr>* queue, int serial)
{
    // 取出流的 time_base，用于把 PTS 换算为微秒
    AVRational tb = {0, 1};
    if (ctx == m_videoCodecCtx.get() && m_videoStreamIdx >= 0)
        tb = m_fmtCtx->streams[m_videoStreamIdx]->time_base;
    else if (ctx == m_audioCodecCtx.get() && m_audioStreamIdx >= 0)
        tb = m_fmtCtx->streams[m_audioStreamIdx]->time_base;

    return m_streamFrameDecoder.sendPacket(
        ctx,
        pkt,
        tb,
        [this, ctx, queue, serial](AVFramePtr frame) -> bool
        {
            if (ctx == m_videoCodecCtx.get())
            {
                ++m_decodePerf.stats().decodedVideoFrames;
                m_decodePerf.stats().sourcePixelFormat = frame->format;

                if (frame->pts != AV_NOPTS_VALUE)
                    emit positionChanged(frame->pts / 1000);

                frame = m_videoFrameProcessor.prepareForQueue(
                    std::move(frame),
                    m_hardwareDecoder.get(),
                    m_videoToolboxDirectRenderingEnabled.loadRelaxed() != 0,
                    m_decodePerf.stats());
                if (!frame)
                {
                    m_decodePerf.maybeLog(m_activeVideoDecoderName);
                    return true;
                }

                m_decodePerf.stats().cpuPixelFormat = frame->format;

                if (m_audioStreamIdx >= 0)
                {
                    // 有音频时以音频时钟为主，视频落后可丢帧追赶。
                    if (!queue->tryPush(std::move(frame), serial))
                    {
                        ++m_decodePerf.stats().queueDroppedVideoFrames;
                        LOG_DEBUG("FFmpegDecoder: video queue full, frame dropped");
                    }
                    else
                    {
                        ++m_decodePerf.stats().queuedVideoFrames;
                    }
                }
                else
                {
                    // 无音频时视频队列就是唯一节奏来源，必须保留背压避免跳帧。
                    if (!queue->push(std::move(frame), serial))
                        return false;
                    ++m_decodePerf.stats().queuedVideoFrames;
                }
                m_decodePerf.maybeLog(m_activeVideoDecoderName);
            }
            else
            {
                // 音频：阻塞push，保证时钟不断
                if (!queue->push(std::move(frame), serial))
                    return false;
            }
            return true;
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// seek 实现
// ─────────────────────────────────────────────────────────────────────────────
void FFmpegDecoder::doSeek(qint64 posMs, int serial)
{

    LOG_INFO("FFmpegDecoder: seek to {}ms", posMs);

    // 将毫秒转换为 AV_TIME_BASE 单位（微秒）
    qint64 seekTarget = posMs * AV_TIME_BASE / 1000;

    int ret = av_seek_frame(m_fmtCtx.get(), -1, seekTarget, AVSEEK_FLAG_BACKWARD);
    if (ret < 0)
    {
        LOG_WARN("FFmpegDecoder: seek failed: {}", av_err(ret));
        return;
    }

    // flush 解码器缓冲
    if (m_videoCodecCtx)
        avcodec_flush_buffers(m_videoCodecCtx.get());
    if (m_audioCodecCtx)
        avcodec_flush_buffers(m_audioCodecCtx.get());

    // 切换 serial，通知消费侧丢弃旧帧
    m_flushSerial = serial;
    m_videoQueue->flush();
    m_audioQueue->flush();

    LOG_INFO("FFmpegDecoder: seek done, new serial={}", m_flushSerial);
}

// ─────────────────────────────────────────────────────────────────────────────
// 关闭
// ─────────────────────────────────────────────────────────────────────────────
void FFmpegDecoder::closeInternal()
{
    if (m_hardwareDecoder)
        m_hardwareDecoder->reset();
    m_hardwareDecoder.reset();
    m_videoCodecCtx.reset();
    m_audioCodecCtx.reset();
    m_videoFrameProcessor.reset();
    m_fmtCtx.reset();
    m_videoStreamIdx = -1;
    m_audioStreamIdx = -1;
    m_durationMs = 0;
    m_videoWidth = 0;
    m_videoHeight = 0;
    m_videoFps = 0.0;
    m_audioChannels = 0;
    m_audioSampleRate = 0;
    m_audioChannelLayoutMask = 0;
    m_audioSampleFmt = AV_SAMPLE_FMT_FLTP;
    m_videoToolboxDirectRenderingEnabled.storeRelaxed(0);
    m_activeVideoDecoderName = QStringLiteral("software");
    m_decodePerf.reset();
    LOG_DEBUG("FFmpegDecoder: closed");
}
