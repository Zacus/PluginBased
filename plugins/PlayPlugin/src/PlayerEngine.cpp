#include "PlayerEngine.h"
#include "Logger.h"
#include "PlaybackContext.h"

#include <QFileInfo>
// ─────────────────────────────────────────────────────────────────────────────
// 构造 / 析构
// ─────────────────────────────────────────────────────────────────────────────
PlayerEngine::PlayerEngine(QObject* parent) : QObject(parent)
{
    // 构造时就创建好组件，队列在成员初始化时已构造
    m_decoder = std::make_unique<FFmpegDecoder>(&m_videoQueue, &m_audioQueue);
    m_audioRenderer = std::make_unique<AudioRenderer>(&m_audioQueue, &m_clock);
    m_videoRenderer = std::make_unique<VideoRenderer>(&m_videoQueue, &m_clock);

    // ── FFmpegDecoder 信号 ────────────────────────────────────────────────────
    connect(m_decoder.get(), &FFmpegDecoder::mediaInfoReady, this, &PlayerEngine::onMediaInfoReady);
    connect(m_decoder.get(), &FFmpegDecoder::errorOccurred, this, &PlayerEngine::onDecoderError);
    connect(m_decoder.get(), &FFmpegDecoder::endOfFile, this, &PlayerEngine::onEndOfFile);
    connect(m_decoder.get(), &FFmpegDecoder::positionChanged, this, &PlayerEngine::onDecoderPosition);
    connect(m_decoder.get(), &FFmpegDecoder::seekCompleted, this,
            &PlayerEngine::onDecoderSeekCompleted);

    // ── AudioRenderer 信号 ────────────────────────────────────────────────────
    connect(m_audioRenderer.get(), &AudioRenderer::positionChanged, this,
            &PlayerEngine::onAudioPosition);
    connect(m_audioRenderer.get(), &AudioRenderer::errorOccurred, this,
            &PlayerEngine::onDecoderError);
    connect(m_audioRenderer.get(), &AudioRenderer::endOfAudio, this,
            &PlayerEngine::onEndOfAudio);

    // ── VideoRenderer 信号 ────────────────────────────────────────────────────
    connect(m_videoRenderer.get(), &VideoRenderer::positionChanged, this,
            &PlayerEngine::onVideoPosition);
    connect(m_videoRenderer.get(), &VideoRenderer::endOfVideo, this, &PlayerEngine::onEndOfVideo);

    LOG_DEBUG("PlayerEngine created");
    PlaybackContext::instance().registerEngine(this);
}

PlayerEngine::~PlayerEngine()
{
    PlaybackContext::instance().unregisterEngine(this);
    stopAllComponents();
    LOG_DEBUG("PlayerEngine destroyed");
}

// ─────────────────────────────────────────────────────────────────────────────
// QML 侧绑定视频输出组件
// ─────────────────────────────────────────────────────────────────────────────
void PlayerEngine::setSurface(FFmpegSurface* surface)
{
    // 断开旧的连接
    if (m_surface)
        disconnect(m_videoRenderer.get(), &VideoRenderer::frameReady, m_surface.data(),
                   &FFmpegSurface::onFrameReady);

    m_surface = surface;

    if (m_surface)
        connect(m_videoRenderer.get(), &VideoRenderer::frameReady, m_surface.data(),
                &FFmpegSurface::onFrameReady,
                Qt::DirectConnection); // 主线程 → 主线程，直接调用
}

// ─────────────────────────────────────────────────────────────────────────────
// 属性写方法
// ─────────────────────────────────────────────────────────────────────────────
void PlayerEngine::setVolume(float v)
{
    v = qBound(0.0f, v, 1.0f);
    if (qFuzzyCompare(m_volume, v))
        return;
    m_volume = v;
    m_audioRenderer->setVolume(v);
    emit volumeChanged(m_volume);
}

void PlayerEngine::setMuted(bool m)
{
    if (m_muted == m)
        return;
    m_muted = m;
    m_audioRenderer->setMuted(m);
    emit mutedChanged(m_muted);
}

// ─────────────────────────────────────────────────────────────────────────────
// 播放控制
// ─────────────────────────────────────────────────────────────────────────────
void PlayerEngine::open(const QUrl& url)
{
    LOG_INFO("PlayerEngine: open({})", url.toString().toStdString());
    stop();

    if (m_surface)
        m_surface->clear();

    m_currentUrl = url;
    m_hasAudio = false;
    m_hasVideo = false;
    m_decoderFinished = false;
    m_audioFinished = false;
    m_videoFinished = false;
    m_mediaFinished = false;
    m_seekGeneration = 0;
    m_audioRenderer->setAcceptedSerial(0);
    m_videoRenderer->setAcceptedSerial(0);

    // 重置队列 abort 状态（stop 时 abort 了，open 前需要恢复）
    m_videoQueue.resetAbort();
    m_audioQueue.resetAbort();

    // 重置时钟
    m_clock.invalidate();

    // 通知解码器打开文件（异步，解码器线程内执行）
    m_decoder->openFile(url);

    setState(Playing);
}

void PlayerEngine::play()
{
    if (m_state == Playing)
        return;
    m_decoder->setPaused(false);
    m_audioRenderer->setPaused(false);
    m_videoRenderer->setPaused(false);
    // VideoRenderer 定时器持续运行：
    //   暂停期间音频时钟冻结，held 帧的 decide() 始终返回 Wait，定时器空转不渲染。
    //   恢复后时钟继续，held 帧正常渲染，无需额外控制定时器。
    setState(Playing);
    LOG_INFO("PlayerEngine: play");
}

void PlayerEngine::pause()
{
    if (m_state != Playing)
        return;
    if (m_decoder)
        m_decoder->setPaused(true);
    if (m_audioRenderer)
        m_audioRenderer->setPaused(true);
    if (m_videoRenderer)
        m_videoRenderer->setPaused(true);
    // m_decoder->setPaused(true);
    // m_audioRenderer->setPaused(true);
    // VideoRenderer 定时器刻意不停：
    //   音频 sink suspend 后 processedUSecs() 冻结，m_heldEntry 的 PTS 超前于冻结时钟，
    //   decide() 持续返回 Wait，不会渲染新帧，也不会丢帧。
    //   FFmpegSurface 保持最后一帧的 GPU 纹理，画面不黑屏。
    setState(Paused);
    LOG_INFO("PlayerEngine: pause");
}

void PlayerEngine::stop()
{
    if (m_state == Stopped)
        return;
    stopAllComponents();
    setState(Stopped);
    m_position = 0;
    m_duration = 0;
    m_hasAudio = false;
    m_hasVideo = false;
    m_decoderFinished = false;
    m_audioFinished = false;
    m_videoFinished = false;
    m_mediaFinished = true;
    delete m_mediaInfo;
    m_mediaInfo = nullptr;

    emit positionChanged(m_position);
    emit durationChanged(m_duration);
    emit currentMediaChanged(nullptr);
    if (m_surface)
        m_surface->clear();
    LOG_INFO("PlayerEngine: stop");
}

void PlayerEngine::seek(qint64 positionMs)
{
    if (m_state == Stopped && !m_mediaFinished)
        return;
    LOG_INFO("PlayerEngine: seek to {}ms", positionMs);

    const bool resumeAfterSeek = m_mediaFinished;
    if (m_mediaFinished)
    {
        m_decoderFinished = false;
        m_audioFinished = !m_hasAudio;
        m_videoFinished = !m_hasVideo;
        m_mediaFinished = false;
    }

    const int seekGeneration = ++m_seekGeneration;

    // 通知各组件 seek
    m_videoRenderer->beginSeek(seekGeneration);
    m_audioRenderer->setAcceptedSerial(seekGeneration);
    m_decoder->seekTo(positionMs, seekGeneration);
    m_audioRenderer->flush();
    m_videoRenderer->flush();

    // 时钟重置（flush 后由 AudioRenderer 重新建立）
    m_clock.invalidate();

    // 立即更新 UI 进度条（不等音频时钟重建）
    m_position = positionMs;
    emit positionChanged(m_position);

    if (resumeAfterSeek)
    {
        m_decoder->setPaused(false);
        m_audioRenderer->setPaused(false);
        m_videoRenderer->setPaused(false);
        setState(Playing);
    }
}

void PlayerEngine::togglePlayPause()
{
    if (m_state == Playing)
        pause();
    else if (m_state == Paused)
        play();
}

// ─────────────────────────────────────────────────────────────────────────────
// 停止所有组件
// ─────────────────────────────────────────────────────────────────────────────
void PlayerEngine::stopAllComponents()
{
    // 解码器先停：abort 队列，使阻塞的 push 立即返回
    m_decoder->stopDecoding();
    m_decoder->wait();

    // 音频渲染器
    m_audioRenderer->stopRenderer();
    m_audioRenderer->wait();

    // 视频渲染器（主线程，只需停止定时器）
    m_videoRenderer->stop();
    m_videoRenderer->reset();

    // 清空队列
    m_videoQueue.flush();
    m_audioQueue.flush();

    // 重置队列 abort 状态，下次 open 时可以正常使用
    m_videoQueue.resetAbort();
    m_audioQueue.resetAbort();
}

// ─────────────────────────────────────────────────────────────────────────────
// 来自 FFmpegDecoder 的信号处理
// ─────────────────────────────────────────────────────────────────────────────
void PlayerEngine::onMediaInfoReady(qint64 durationMs, int width, int height, double fps,
                                    int sampleRate, int channels, quint64 channelLayoutMask,
                                    int sampleFmt, const QString& format)
{
    LOG_INFO("PlayerEngine: mediaInfoReady dur={}ms {}x{} @{:.1f}fps {}Hz {}ch fmt={}", durationMs,
             width, height, fps, sampleRate, channels, sampleFmt);

    m_duration = durationMs;
    emit durationChanged(m_duration);

    // VideoRenderer 会在真正消费 AVFrame 时读取颜色范围 / 色彩空间等信息，
    // 这里不再提前做 CPU 侧的视频格式初始化。
    m_hasAudio = sampleRate > 0 && channels > 0;
    m_hasVideo = width > 0 && height > 0;
    m_videoRenderer->setAudioClockEnabled(m_hasAudio);
    m_videoRenderer->setPaused(false);

    if (m_hasAudio)
    {
        m_audioRenderer->setSourceFormat(sampleRate, channels,
                                         static_cast<AVSampleFormat>(sampleFmt),
                                         channelLayoutMask);
        if (!m_audioRenderer->isRunning())
            m_audioRenderer->start(QThread::HighPriority);
    }

    if (m_hasVideo)
        m_videoRenderer->start();

    // 更新 MediaInfo（QML 侧显示文件信息）
    // onMediaInfoReady 通过 Qt::AutoConnection 在主线程执行，安全
    delete m_mediaInfo;
    m_mediaInfo = new MediaInfo(m_currentUrl, QFileInfo(m_currentUrl.toLocalFile()).baseName(),
                                durationMs, width, height, format, this);
    emit currentMediaChanged(m_mediaInfo);
}

void PlayerEngine::onDecoderError(const QString& msg)
{
    setError(msg);
    stopAllComponents();
    setState(Stopped);
    m_position = 0;
    m_duration = 0;
    m_hasAudio = false;
    m_hasVideo = false;
    m_decoderFinished = false;
    m_audioFinished = false;
    m_videoFinished = false;
    m_mediaFinished = true;
    emit positionChanged(m_position);
    emit durationChanged(m_duration);
}

void PlayerEngine::onEndOfFile()
{
    LOG_INFO("PlayerEngine: end of file");
    // 不立即 stop，等 VideoRenderer 处理完剩余帧后发 endOfVideo
    // 如果有音频，也要等 AudioRenderer 消费到 EOF，避免截断音频缓冲。
    m_decoderFinished = true;
    if (!m_hasAudio)
        m_audioFinished = true;
    if (!m_hasVideo)
        m_videoFinished = true;
    maybeFinishMedia();
}

void PlayerEngine::onDecoderPosition(qint64 posMs)
{
    if (m_hasAudio || m_hasVideo)
        return;

    m_position = posMs;
    emit positionChanged(m_position);
}

void PlayerEngine::onDecoderSeekCompleted(int generation, int serial)
{
    m_audioRenderer->setAcceptedSerial(serial);
    m_videoRenderer->completeSeek(generation, serial);
}

// ─────────────────────────────────────────────────────────────────────────────
// 来自 AudioRenderer 的信号处理
// ─────────────────────────────────────────────────────────────────────────────
void PlayerEngine::onAudioPosition(qint64 posMs)
{
    m_position = posMs;
    emit positionChanged(m_position);
}

void PlayerEngine::onEndOfAudio()
{
    LOG_INFO("PlayerEngine: end of audio");
    m_audioFinished = true;
    maybeFinishMedia();
}

// ─────────────────────────────────────────────────────────────────────────────
// 来自 VideoRenderer 的信号处理
// ─────────────────────────────────────────────────────────────────────────────
void PlayerEngine::onVideoPosition(qint64 posMs)
{
    if (m_hasAudio)
        return;

    m_position = posMs;
    emit positionChanged(m_position);
}

void PlayerEngine::onEndOfVideo()
{
    LOG_INFO("PlayerEngine: end of video");
    m_videoFinished = true;
    maybeFinishMedia();
}

// ─────────────────────────────────────────────────────────────────────────────
// 内部状态管理
// ─────────────────────────────────────────────────────────────────────────────
void PlayerEngine::setState(PlaybackState s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit playbackStateChanged(static_cast<int>(m_state));
}

void PlayerEngine::setError(const QString& msg)
{
    m_errorString = msg;
    LOG_ERROR("PlayerEngine error: {}", msg.toStdString());
    emit errorOccurred(msg);
}

void PlayerEngine::maybeFinishMedia()
{
    if (!m_decoderFinished)
        return;
    if (m_hasAudio && !m_audioFinished)
        return;
    if (m_hasVideo && !m_videoFinished)
        return;

    finishMedia();
}

void PlayerEngine::finishMedia()
{
    if (m_mediaFinished)
        return;

    m_mediaFinished = true;
    m_decoder->setPaused(true);
    m_audioRenderer->setPaused(true);
    m_videoRenderer->setPaused(true);
    setState(Paused);
    emit endOfMedia();
}
