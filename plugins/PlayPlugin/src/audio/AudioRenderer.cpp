#include "audio/AudioRenderer.h"
#include "Logger.h"

#include <QAudioDevice>
#include <QMediaDevices>
#include <cstring>

AudioRenderer::AudioRenderer(AudioFrameQueue* queue, ClockSync* clock, QObject* parent)
    : QThread(parent), m_queue(queue), m_clock(clock)
{
}

AudioRenderer::~AudioRenderer()
{
    stopRenderer();
    wait();
}

// ─────────────────────────────────────────────────────────────────────────────
// 主线程控制接口
// ─────────────────────────────────────────────────────────────────────────────
void AudioRenderer::setVolume(float v)
{
    QMutexLocker lk(&m_paramMutex);
    m_volume = qBound(0.0f, v, 1.0f);
    // 不在这里访问 m_sink（主线程调用，m_sink 属于音频线程）
    // 音频线程在每帧渲染前通过 applyPendingParams() 统一应用
    m_paramDirty = true;
}

void AudioRenderer::setMuted(bool muted)
{
    QMutexLocker lk(&m_paramMutex);
    m_muted = muted;
    m_paramDirty = true;
}

void AudioRenderer::setPaused(bool paused)
{
    // 只更新标志位。QAudioSink 不是线程安全的，suspend()/resume()
    // 必须在音频线程内部执行，由 run() 循环检测 flag 变化后统一调用。
    m_paused.storeRelaxed(paused ? 1 : 0);
}

void AudioRenderer::setAcceptedSerial(int serial)
{
    m_acceptedSerial.storeRelaxed(serial);
}

void AudioRenderer::stopRenderer()
{
    m_stop.storeRelaxed(1);
    m_queue->abort();
}

void AudioRenderer::flush()
{
    m_flush.storeRelaxed(1);
    if (m_queue)
        m_queue->flush();
    m_clock->invalidate();
}

void AudioRenderer::setSourceFormat(int sampleRate, int channels, AVSampleFormat fmt,
                                    quint64 channelLayoutMask)
{
    m_srcSampleRate = sampleRate;
    m_srcChannels = channels;
    m_srcChannelLayoutMask = channelLayoutMask;
    m_srcFmt = fmt;
}

// ─────────────────────────────────────────────────────────────────────────────
// 线程主函数
// ─────────────────────────────────────────────────────────────────────────────
void AudioRenderer::run()
{
    // 每次线程启动时重置控制标志。
    // stopRenderer() 会把 m_stop 置 1，但 QThread::start() 不会自动重置它。
    // 若不重置，第二次 open() 启动的新线程会因 m_stop=1 立刻退出（无声音）。
    // 同理，若上次是"暂停时关闭文件"，m_paused=1 会让新线程立刻进入 suspend，
    // 时钟永远冻结，视频永远黑屏。
    m_stop.storeRelaxed(0);
    m_paused.storeRelaxed(0);
    m_flush.storeRelaxed(0);
    resetClockState();

    LOG_INFO("AudioRenderer thread started");

    // QAudioSink 必须在使用它的线程里创建
    QAudioFormat fmt;
    fmt.setSampleRate(TARGET_SAMPLE_RATE);
    fmt.setChannelCount(TARGET_CHANNELS);
    fmt.setSampleFormat(QAudioFormat::Float);

    const QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();
    m_sink = new QAudioSink(defaultDevice, fmt);
    m_device = m_sink->start();

    if (!m_device)
    {
        emit errorOccurred("AudioRenderer: 无法打开音频输出设备");
        delete m_sink;
        m_sink = nullptr;
        return;
    }

    // 初始应用音量（此时在音频线程，访问 m_sink 安全）
    {
        QMutexLocker lk(&m_paramMutex);
        m_sink->setVolume(m_muted ? 0.0f : m_volume);
        m_paramDirty = false;
    }

    // 初始化重采样器
    if (!initSwrContext())
    {
        emit errorOccurred("AudioRenderer: 重采样器初始化失败");
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
        return;
    }

    LOG_INFO("AudioRenderer: QAudioSink started ({}Hz {}ch float)", TARGET_SAMPLE_RATE,
             TARGET_CHANNELS);

    // ── 主渲染循环 ────────────────────────────────────────────────────────────
    VideoFrameQueue::Entry entry;
    bool wasPaused = false; // 用于检测 paused/resumed 状态变化

    while (!m_stop.loadRelaxed())
    {

        const bool isPaused = m_paused.loadRelaxed() != 0;

        // ── 检测 pause/resume 状态切换，在音频线程内安全调用 QAudioSink ──────
        // QAudioSink 不是线程安全的，suspend()/resume() 必须在创建它的线程里调用。
        // 主线程的 setPaused() 只修改 m_paused 标志，
        // 由此处（音频线程）在检测到切换时统一执行实际的 sink 操作。
        if (isPaused != wasPaused)
        {
            if (isPaused)
            {
                m_sink->suspend();
                m_clock->setPaused(true);
                LOG_DEBUG("AudioRenderer: sink suspended");
            }
            else
            {
                m_sink->resume();
                m_clock->setPaused(false);
                // resume 后 processedUSecs() 从暂停点继续递增，
                // 重置发信号节流起点，避免 resume 后延迟 100ms 才更新 UI
                const qint64 clockUs = m_clock->audioClock();
                m_lastEmitUs = (clockUs == ClockSync::INVALID_CLOCK) ? 0 : clockUs;
                LOG_DEBUG("AudioRenderer: sink resumed");
            }
            wasPaused = isPaused;
        }

        // flush 请求（seek）
        if (m_flush.loadRelaxed())
        {
            if (!handleFlush())
                break;
            if (isPaused)
                m_sink->suspend();
        }

        // 暂停中：不消费队列，低功耗等待
        if (isPaused)
        {
            QThread::msleep(10);
            continue;
        }

        // 应用主线程挂起的音量/静音变更（在音频线程操作 sink，线程安全）
        {
            QMutexLocker lk(&m_paramMutex);
            if (m_paramDirty)
            {
                m_sink->setVolume(m_muted ? 0.0f : m_volume);
                m_paramDirty = false;
            }
        }

        if (!m_queue->pop(entry))
            break; // abort

        if (entry.serial != m_acceptedSerial.loadRelaxed())
            continue;

        if (entry.eof)
        {
            // 音频流结束，等待下一个文件
            LOG_INFO("AudioRenderer: end of audio serial={}", entry.serial);
            m_clock->invalidate();
            emit endOfAudio();
            continue;
        }

        if (!entry.frame)
            continue;

        renderFrame(entry.frame.get());
    }

    m_sink->stop();
    delete m_sink;
    m_sink = nullptr;
    m_device = nullptr;

    LOG_INFO("AudioRenderer thread exiting");
}

// ─────────────────────────────────────────────────────────────────────────────
// 初始化 libswresample 重采样器
// ─────────────────────────────────────────────────────────────────────────────
bool AudioRenderer::initSwrContext()
{
    SwrContext* swr = nullptr;

    AVChannelLayout srcLayout {};
    int ret = 0;
    if (m_srcChannelLayoutMask != 0)
        ret = av_channel_layout_from_mask(&srcLayout, m_srcChannelLayoutMask);

    if (m_srcChannelLayoutMask == 0 || ret < 0)
    {
        av_channel_layout_default(&srcLayout, m_srcChannels);
        ret = 0;
    }

    // 目标声道布局（固定立体声）
    AVChannelLayout dstLayout = AV_CHANNEL_LAYOUT_STEREO;

    ret = swr_alloc_set_opts2(
        &swr,
        &dstLayout,         // 目标声道布局
        AV_SAMPLE_FMT_FLT,  // 目标格式：packed float（交织），单 buffer 即可
                            // 不用 FLTP（planar），因为 planar 需要每声道独立指针数组，
                            // 用单指针传入 swr_convert 会写野指针 → 崩溃
        TARGET_SAMPLE_RATE, // 目标采样率
        &srcLayout,         // 源声道布局
        m_srcFmt,           // 源格式
        m_srcSampleRate,    // 源采样率
        0, nullptr);

    av_channel_layout_uninit(&srcLayout);

    if (ret < 0 || !swr)
    {
        LOG_ERROR("AudioRenderer: swr_alloc_set_opts2 failed: {}", av_err(ret));
        return false;
    }

    ret = swr_init(swr);
    if (ret < 0)
    {
        swr_free(&swr);
        LOG_ERROR("AudioRenderer: swr_init failed: {}", av_err(ret));
        return false;
    }

    m_swrCtx.reset(swr);
    LOG_INFO("AudioRenderer: swresample OK — {}Hz {}ch {} → {}Hz {}ch float-interleaved",
             m_srcSampleRate, m_srcChannels, av_get_sample_fmt_name(m_srcFmt), TARGET_SAMPLE_RATE,
             TARGET_CHANNELS);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 渲染单帧音频
// ─────────────────────────────────────────────────────────────────────────────
void AudioRenderer::renderFrame(AVFrame* frame)
{
    if (!m_swrCtx || !m_device)
        return;

    // 计算重采样后的样本数
    const int dstSamples = static_cast<int>(
        av_rescale_rnd(swr_get_delay(m_swrCtx.get(), m_srcSampleRate) + frame->nb_samples,
                       TARGET_SAMPLE_RATE, m_srcSampleRate, AV_ROUND_UP));

    // swr 目标格式为 AV_SAMPLE_FMT_FLT（packed/interleaved），
    // 单个缓冲区即可，格式为 [L0,R0,L1,R1,...]，与 QAudioSink 期望完全一致。
    const int bufBytes = dstSamples * TARGET_CHANNELS * static_cast<int>(sizeof(float));
    std::vector<uint8_t> outBuf(bufBytes);
    uint8_t* outPtr = outBuf.data();

    const int actualSamples =
        swr_convert(m_swrCtx.get(), &outPtr, dstSamples, const_cast<const uint8_t**>(frame->data),
                    frame->nb_samples);

    if (actualSamples < 0)
    {
        LOG_WARN("AudioRenderer: swr_convert failed: {}", av_err(actualSamples));
        return;
    }

    const qint64 writeBytes = actualSamples * TARGET_CHANNELS * static_cast<int>(sizeof(float));

    // 分批写入（QAudioSink 内部有缓冲区，write 可能不完全写入）
    const char* ptr = reinterpret_cast<const char*>(outBuf.data());
    qint64 remaining = writeBytes;
    while (remaining > 0 && !m_stop.loadRelaxed() && !m_flush.loadRelaxed())
    {
        const qint64 written = m_device->write(ptr, remaining);
        if (written <= 0)
        {
            QThread::msleep(1); // 缓冲区满，稍等
            continue;
        }
        ptr += written;
        remaining -= written;
    }

    if (m_flush.loadRelaxed())
        return;

    updateClock(frame->pts);
}

// ─────────────────────────────────────────────────────────────────────────────
// seek/flush 处理
// ─────────────────────────────────────────────────────────────────────────────
bool AudioRenderer::handleFlush()
{
    m_flush.storeRelaxed(0);
    resetClockState();

    if (m_swrCtx)
        swr_init(m_swrCtx.get());

    if (!m_sink)
        return true;

    m_sink->reset();
    m_device = m_sink->start();
    if (!m_device)
    {
        emit errorOccurred("AudioRenderer: seek 后无法重新打开音频输出设备");
        return false;
    }

    QMutexLocker lk(&m_paramMutex);
    m_sink->setVolume(m_muted ? 0.0f : m_volume);
    m_paramDirty = false;
    return true;
}

void AudioRenderer::resetClockState()
{
    m_lastEmitUs = 0;
    m_startPts = AV_NOPTS_VALUE;
    m_startSinkProcessedUs = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// 更新音频时钟
// ─────────────────────────────────────────────────────────────────────────────
void AudioRenderer::updateClock(qint64 framePtsUs)
{
    if (!m_sink)
        return;

    const qint64 processedUs = m_sink->processedUSecs();
    if (m_startPts == AV_NOPTS_VALUE)
    {
        m_startPts = (framePtsUs != AV_NOPTS_VALUE) ? framePtsUs : 0;
        m_startSinkProcessedUs = processedUs;
    }

    const qint64 elapsedUs = qMax<qint64>(0, processedUs - m_startSinkProcessedUs);
    const qint64 clockUs = m_startPts + elapsedUs;
    m_clock->setAudioClock(clockUs);

    // 每 100ms 发一次位置信号（不需要每帧都发）
    if (clockUs - m_lastEmitUs >= 100'000)
    {
        m_lastEmitUs = clockUs;
        emit positionChanged(clockUs / 1000);
    }
}
