#include "playback/PlayerEngine.h"
#include "Logger.h"
#include "playback/PlaybackContext.h"
#include "playback/PlaybackPipeline.h"

#include <QFileInfo>
// ─────────────────────────────────────────────────────────────────────────────
// 构造 / 析构
// ─────────────────────────────────────────────────────────────────────────────
PlayerEngine::PlayerEngine(QObject* parent) : QObject(parent)
{
    m_pipeline = std::make_unique<PlaybackPipeline>(this);

    connect(m_pipeline.get(), &PlaybackPipeline::mediaInfoReady,
            this, &PlayerEngine::onMediaInfoReady);
    connect(m_pipeline.get(), &PlaybackPipeline::errorOccurred,
            this, &PlayerEngine::onDecoderError);
    connect(m_pipeline.get(), &PlaybackPipeline::endOfFile,
            this, &PlayerEngine::onEndOfFile);
    connect(m_pipeline.get(), &PlaybackPipeline::decoderPositionChanged,
            this, &PlayerEngine::onDecoderPosition);
    connect(m_pipeline.get(), &PlaybackPipeline::seekCompleted,
            this, &PlayerEngine::onDecoderSeekCompleted);
    connect(m_pipeline.get(), &PlaybackPipeline::audioPositionChanged,
            this, &PlayerEngine::onAudioPosition);
    connect(m_pipeline.get(), &PlaybackPipeline::endOfAudio,
            this, &PlayerEngine::onEndOfAudio);
    connect(m_pipeline.get(), &PlaybackPipeline::videoPositionChanged,
            this, &PlayerEngine::onVideoPosition);
    connect(m_pipeline.get(), &PlaybackPipeline::endOfVideo,
            this, &PlayerEngine::onEndOfVideo);

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
    m_pipeline->setSurface(surface);
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
    m_pipeline->setVolume(v);
    emit volumeChanged(m_volume);
}

void PlayerEngine::setMuted(bool m)
{
    if (m_muted == m)
        return;
    m_muted = m;
    m_pipeline->setMuted(m);
    emit mutedChanged(m_muted);
}

// ─────────────────────────────────────────────────────────────────────────────
// 播放控制
// ─────────────────────────────────────────────────────────────────────────────
void PlayerEngine::open(const QUrl& url)
{
    LOG_INFO("PlayerEngine: open({})", url.toString().toStdString());
    stop();

    m_pipeline->clearSurface();

    m_currentUrl = url;
    m_completion.resetForOpen();
    m_seekGeneration = 0;

    // 通知解码器打开文件（异步，解码器线程内执行）
    m_pipeline->openFile(url);

    setState(Playing);
}

void PlayerEngine::play()
{
    if (m_state == Playing)
        return;
    m_pipeline->setPaused(false);
    setState(Playing);
    LOG_INFO("PlayerEngine: play");
}

void PlayerEngine::pause()
{
    if (m_state != Playing)
        return;
    m_pipeline->setPaused(true);
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
    m_completion.resetForStop();
    delete m_mediaInfo;
    m_mediaInfo = nullptr;

    emit positionChanged(m_position);
    emit durationChanged(m_duration);
    emit currentMediaChanged(nullptr);
    m_pipeline->clearSurface();
    LOG_INFO("PlayerEngine: stop");
}

void PlayerEngine::seek(qint64 positionMs)
{
    if (m_state == Stopped && !m_completion.isMediaFinished())
        return;
    LOG_INFO("PlayerEngine: seek to {}ms", positionMs);

    const bool resumeAfterSeek = m_completion.resumeAfterFinishedSeek(m_state == Playing);

    const int seekGeneration = ++m_seekGeneration;

    // 通知各组件 seek
    m_pipeline->seek(positionMs, seekGeneration);

    // 立即更新 UI 进度条（不等音频时钟重建）
    m_position = positionMs;
    emit positionChanged(m_position);

    if (resumeAfterSeek)
    {
        m_pipeline->setPaused(false);
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
    m_pipeline->stopComponents();
}

// ─────────────────────────────────────────────────────────────────────────────
// 来自播放管线的媒体核心信号处理
// ─────────────────────────────────────────────────────────────────────────────
void PlayerEngine::onMediaInfoReady(qint64 durationMs, int width, int height, double fps,
                                    int sampleRate, int channels, quint64 channelLayoutMask,
                                    int sampleFmt, const QString& format)
{
    LOG_INFO("PlayerEngine: mediaInfoReady dur={}ms {}x{} @{:.1f}fps {}Hz {}ch fmt={}", durationMs,
             width, height, fps, sampleRate, channels, sampleFmt);

    m_duration = durationMs;
    emit durationChanged(m_duration);

    m_completion.setStreams(sampleRate > 0 && channels > 0,
                            width > 0 && height > 0);
    m_pipeline->startRenderersForMedia(m_completion.hasAudio(),
                                       m_completion.hasVideo(),
                                       sampleRate,
                                       channels,
                                       channelLayoutMask,
                                       sampleFmt);

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
    m_completion.resetForStop();
    emit positionChanged(m_position);
    emit durationChanged(m_duration);
}

void PlayerEngine::onEndOfFile()
{
    LOG_INFO("PlayerEngine: end of file");
    // 不立即 stop，等 SDK runtime 完成音频/视频 drain 后再收尾。
    m_completion.markDecoderFinished();
    maybeFinishMedia();
}

void PlayerEngine::onDecoderPosition(qint64 posMs)
{
    m_position = posMs;
    emit positionChanged(m_position);
}

void PlayerEngine::onDecoderSeekCompleted(int generation, int serial)
{
    Q_UNUSED(generation);
    Q_UNUSED(serial);
}

// ─────────────────────────────────────────────────────────────────────────────
// 来自 SDK runtime 音频 drain/position 信号处理
// ─────────────────────────────────────────────────────────────────────────────
void PlayerEngine::onAudioPosition(qint64 posMs)
{
    m_position = posMs;
    emit positionChanged(m_position);
}

void PlayerEngine::onEndOfAudio()
{
    LOG_INFO("PlayerEngine: end of audio");
    m_completion.markAudioFinished();
    maybeFinishMedia();
}

// ─────────────────────────────────────────────────────────────────────────────
// 来自 SDK runtime 视频 drain/position 信号处理
// ─────────────────────────────────────────────────────────────────────────────
void PlayerEngine::onVideoPosition(qint64 posMs)
{
    if (m_completion.hasAudio())
        return;

    m_position = posMs;
    emit positionChanged(m_position);
}

void PlayerEngine::onEndOfVideo()
{
    LOG_INFO("PlayerEngine: end of video");
    m_completion.markVideoFinished();
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
    if (!m_completion.shouldFinish())
        return;

    finishMedia();
}

void PlayerEngine::finishMedia()
{
    if (!m_completion.finish())
        return;

    m_pipeline->setPaused(true);
    setState(Paused);
    emit endOfMedia();
}
