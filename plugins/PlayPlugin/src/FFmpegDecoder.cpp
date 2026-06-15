#include "FFmpegDecoder.h"
#include "Logger.h"
#include "hw/HardwareDecoderFactory.h"

#include <QFileInfo>

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
AVCodecContext* FFmpegDecoder::createVideoCodecContext(AVStream* stream, const AVCodec* codec)
{
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx)
        return nullptr;

    const int ret = avcodec_parameters_to_context(ctx, stream->codecpar);
    if (ret < 0)
    {
        LOG_WARN("FFmpegDecoder: avcodec_parameters_to_context for video failed: {}",
                 av_err(ret));
        avcodec_free_context(&ctx);
        return nullptr;
    }

    ctx->thread_count = 0;
    return ctx;
}

bool FFmpegDecoder::openVideoCodec(AVStream* stream, const AVCodec* codec)
{
    AVCodecContext* vctx = createVideoCodecContext(stream, codec);
    if (!vctx)
        return false;

    m_hardwareDecoder = createHardwareDecoderBackend(codec, stream->codecpar->codec_id);
    if (m_hardwareDecoder)
    {
        LOG_INFO("FFmpegDecoder: selected hardware decoder {}",
                 m_hardwareDecoder->name().toStdString());
        if (!m_hardwareDecoder->configureContext(vctx))
        {
            LOG_WARN("FFmpegDecoder: hardware setup failed, fallback to software decoding");
            m_hardwareDecoder->reset();
            m_hardwareDecoder.reset();
        }
    }

    int ret = avcodec_open2(vctx, codec, nullptr);
    if (ret < 0 && m_hardwareDecoder)
    {
        LOG_WARN("FFmpegDecoder: hardware codec open failed: {}, fallback to software decoding",
                 av_err(ret));
        avcodec_free_context(&vctx);
        m_hardwareDecoder->reset();
        m_hardwareDecoder.reset();

        vctx = createVideoCodecContext(stream, codec);
        if (!vctx)
            return false;
        ret = avcodec_open2(vctx, codec, nullptr);
    }

    if (ret < 0)
    {
        avcodec_free_context(&vctx);
        LOG_WARN("FFmpegDecoder: failed to open video decoder: {}", av_err(ret));
        return false;
    }

    m_activeVideoDecoderName = m_hardwareDecoder
        ? m_hardwareDecoder->name()
        : QStringLiteral("software");
    m_videoCodecCtx.reset(vctx);
    m_videoWidth = vctx->width;
    m_videoHeight = vctx->height;
    AVRational fr = stream->avg_frame_rate;
    m_videoFps = (fr.den > 0) ? av_q2d(fr) : 25.0;
    return true;
}

bool FFmpegDecoder::openInternal(const QString& path)
{
    LOG_INFO("FFmpegDecoder: opening {}", path.toStdString());

    // ── 打开容器 ──────────────────────────────────────────────────────────────
    AVFormatContext* fmtRaw = nullptr;
    int ret = avformat_open_input(&fmtRaw, path.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0)
    {
        emit errorOccurred(
            QString("无法打开文件: %1 (%2)").arg(path).arg(QString::fromStdString(av_err(ret))));
        return false;
    }
    m_fmtCtx.reset(fmtRaw);

    // ── 探测流信息 ────────────────────────────────────────────────────────────
    ret = avformat_find_stream_info(m_fmtCtx.get(), nullptr);
    if (ret < 0)
    {
        emit errorOccurred(QString("无法读取流信息: %1").arg(QString::fromStdString(av_err(ret))));
        return false;
    }

    // ── 总时长（微秒 → 毫秒）────────────────────────────────────────────────
    m_durationMs = (m_fmtCtx->duration != AV_NOPTS_VALUE) ? (m_fmtCtx->duration / 1000) : 0;

    // ── 格式名称 ─────────────────────────────────────────────────────────────
    m_formatName = QString::fromUtf8(m_fmtCtx->iformat ? m_fmtCtx->iformat->long_name : "unknown");

    // ── 找最佳视频流并打开解码器 ─────────────────────────────────────────────
    m_videoStreamIdx = av_find_best_stream(m_fmtCtx.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_videoStreamIdx >= 0)
    {
        AVStream* vs = m_fmtCtx->streams[m_videoStreamIdx];
        const AVCodec* vcodec = avcodec_find_decoder(vs->codecpar->codec_id);
        if (!vcodec)
        {
            LOG_WARN("FFmpegDecoder: no video decoder for codec_id={}",
                     static_cast<int>(vs->codecpar->codec_id));
            m_videoStreamIdx = -1;
        }
        else
        {
            if (!openVideoCodec(vs, vcodec))
                m_videoStreamIdx = -1;
        }
    }

    // ── 找最佳音频流并打开解码器 ─────────────────────────────────────────────
    m_audioStreamIdx = av_find_best_stream(m_fmtCtx.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (m_audioStreamIdx >= 0)
    {
        AVStream* as = m_fmtCtx->streams[m_audioStreamIdx];
        const AVCodec* acodec = avcodec_find_decoder(as->codecpar->codec_id);
        if (!acodec)
        {
            LOG_WARN("FFmpegDecoder: no audio decoder for codec_id={}",
                     static_cast<int>(as->codecpar->codec_id));
            m_audioStreamIdx = -1;
        }
        else
        {
            AVCodecContext* actx = avcodec_alloc_context3(acodec);
            avcodec_parameters_to_context(actx, as->codecpar);
            ret = avcodec_open2(actx, acodec, nullptr);
            if (ret < 0)
            {
                avcodec_free_context(&actx);
                LOG_WARN("FFmpegDecoder: failed to open audio decoder: {}", av_err(ret));
                m_audioStreamIdx = -1;
            }
            else
            {
                m_audioCodecCtx.reset(actx);
                m_audioChannels = actx->ch_layout.nb_channels;
                m_audioSampleRate = actx->sample_rate;
                m_audioChannelLayoutMask =
                    (actx->ch_layout.order == AV_CHANNEL_ORDER_NATIVE)
                        ? static_cast<quint64>(actx->ch_layout.u.mask)
                        : 0;
                m_audioSampleFmt = static_cast<int>(actx->sample_fmt);
            }
        }
    }

    if (m_videoStreamIdx < 0 && m_audioStreamIdx < 0)
    {
        emit errorOccurred("文件中未找到可解码的视频流或音频流");
        return false;
    }

    LOG_INFO("FFmpegDecoder: opened OK — duration={}ms video={}x{} @{:.1f}fps audio={}ch@{}Hz",
             m_durationMs, m_videoWidth, m_videoHeight, m_videoFps, m_audioChannels,
             m_audioSampleRate);
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
    int ret = avcodec_send_packet(ctx, pkt);
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
    {
        LOG_WARN("FFmpegDecoder: avcodec_send_packet: {}", av_err(ret));
        return false;
    }

    // 取出流的 time_base，用于把 PTS 换算为微秒
    AVRational tb = {0, 1};
    if (ctx == m_videoCodecCtx.get() && m_videoStreamIdx >= 0)
        tb = m_fmtCtx->streams[m_videoStreamIdx]->time_base;
    else if (ctx == m_audioCodecCtx.get() && m_audioStreamIdx >= 0)
        tb = m_fmtCtx->streams[m_audioStreamIdx]->time_base;

    while (true)
    {
        auto frame = make_frame();
        ret = avcodec_receive_frame(ctx, frame.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0)
        {
            LOG_WARN("FFmpegDecoder: avcodec_receive_frame: {}", av_err(ret));
            return false;
        }

        // 把 PTS 统一换算为微秒存入 frame->pts
        // VideoRenderer 和 AudioClock 均以微秒为单位，后续直接使用
        qint64 pts = frame->best_effort_timestamp;
        if (pts == AV_NOPTS_VALUE)
            pts = frame->pts;
        if (pts != AV_NOPTS_VALUE && tb.den > 0)
            frame->pts = av_rescale_q(pts, tb, {1, AV_TIME_BASE});
        else
            frame->pts = AV_NOPTS_VALUE;

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
                continue;
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
    }
    return true;
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
