#include "playback/PlayerEngine.h"
#include "Logger.h"
#include "playback/PlaybackContext.h"
#include "playback/PlaybackPipeline.h"
#include "playback/PlaybackSeek.h"
#include "media_sdk/runtime/PlaybackRate.h"

#include <QFileInfo>

#include <algorithm>
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
    connect(m_pipeline.get(), &PlaybackPipeline::playbackRateChanged,
            this, &PlayerEngine::onPlaybackRateChanged);
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

void PlayerEngine::setPlaybackRate(double playbackRate)
{
    if (!media_sdk::runtime::isPlaybackRateSupported(playbackRate)) {
        emit playbackRateChanged(m_playbackRate);
        setError(tr("Playback rate must be between 0.5x and 2.0x"));
        return;
    }
    if (m_playbackRateChangePending) {
        emit playbackRateChanged(m_playbackRate);
        return;
    }
    if (media_sdk::runtime::playbackRatesEqual(m_playbackRate, playbackRate))
        return;

    setPlaybackRateChangePending(true);
    m_pipeline->setPlaybackRate(playbackRate);
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
    m_seekState.reset();
    refreshCanSeekForward();
    setPlaybackRateChangePending(false);

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

    m_position = 0;
    m_duration = 0;
    m_completion.resetForStop();
    m_seekState.reset();
    MediaInfo* const previousMediaInfo = m_mediaInfo;
    m_mediaInfo = nullptr;
    const bool canSeekForwardDidChange = updateCanSeekForward();

    stopAllComponents();
    m_pipeline->clearSurface();
    setState(Stopped);
    setPlaybackRateChangePending(false);
    delete previousMediaInfo;
    if (canSeekForwardDidChange)
        emit canSeekForwardChanged(m_canSeekForward);

    emit positionChanged(m_position);
    emit durationChanged(m_duration);
    emit currentMediaChanged(nullptr);
    LOG_INFO("PlayerEngine: stop");
}

void PlayerEngine::seek(qint64 positionMs)
{
    if (!m_mediaInfo || m_duration <= 0)
        return;

    const qint64 targetPositionMs = std::clamp(positionMs, qint64 { 0 }, m_duration);
    LOG_INFO("PlayerEngine: seek to {}ms", targetPositionMs);

    const bool resumeAfterSeek = m_completion.resumeAfterFinishedSeek(m_state == Playing);

    const int seekGeneration = m_seekState.begin(targetPositionMs);
    if (seekGeneration == 0)
        return;
    const quint64 seekResetVersion = m_seekState.resetVersion();

    // 通知各组件 seek
    m_pipeline->seek(targetPositionMs, seekGeneration, resumeAfterSeek);
    if (!m_seekState.isPending(seekGeneration))
        return;

    // 立即更新 UI 进度条（不等音频时钟重建）
    m_position = targetPositionMs;
    refreshCanSeekForward();

    emit positionChanged(m_position);

    if (resumeAfterSeek && m_seekState.resetVersion() == seekResetVersion)
    {
        setState(Playing);
    }
}

void PlayerEngine::seekBy(qint64 deltaMs)
{
    if (!m_mediaInfo || (deltaMs > 0 && !m_canSeekForward))
        return;

    const qint64 basePosition = m_seekState.basePosition(m_position);
    const auto target = calculateRelativeSeekTarget(basePosition, m_duration, deltaMs);
    if (target)
        seek(*target);
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
    m_completion.setStreams(sampleRate > 0 && channels > 0,
                            width > 0 && height > 0);

    // 更新 MediaInfo（QML 侧显示文件信息）
    // onMediaInfoReady 通过 Qt::AutoConnection 在主线程执行，安全
    MediaInfo* const previousMediaInfo = m_mediaInfo;
    m_mediaInfo = new MediaInfo(m_currentUrl, QFileInfo(m_currentUrl.toLocalFile()).baseName(),
                                durationMs, width, height, format, this);
    const bool canSeekForwardDidChange = updateCanSeekForward();

    m_pipeline->startRenderersForMedia(m_completion.hasAudio(),
                                       m_completion.hasVideo(),
                                       sampleRate,
                                       channels,
                                       channelLayoutMask,
                                       sampleFmt);
    delete previousMediaInfo;
    if (canSeekForwardDidChange)
        emit canSeekForwardChanged(m_canSeekForward);
    emit durationChanged(m_duration);
    emit currentMediaChanged(m_mediaInfo);
}

void PlayerEngine::onDecoderError(const QString& msg)
{
    m_position = 0;
    m_duration = 0;
    m_completion.resetForStop();
    m_seekState.reset();
    const bool canSeekForwardDidChange = updateCanSeekForward();

    stopAllComponents();
    setState(Stopped);
    setPlaybackRateChangePending(false);
    if (canSeekForwardDidChange)
        emit canSeekForwardChanged(m_canSeekForward);
    setError(msg);
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
    if (!m_seekState.acceptsPositionUpdate())
        return;
    m_position = posMs;
    refreshCanSeekForward();
    emit positionChanged(m_position);
}

void PlayerEngine::onDecoderSeekCompleted(int generation, int serial)
{
    Q_UNUSED(serial);
    (void)m_seekState.complete(generation);
}

void PlayerEngine::onPlaybackRateChanged(double playbackRate)
{
    if (!media_sdk::runtime::isPlaybackRateSupported(playbackRate))
        return;

    m_playbackRate = playbackRate;
    emit playbackRateChanged(m_playbackRate);
    setPlaybackRateChangePending(false);
}

// ─────────────────────────────────────────────────────────────────────────────
// 来自 SDK runtime 音频 drain/position 信号处理
// ─────────────────────────────────────────────────────────────────────────────
void PlayerEngine::onAudioPosition(qint64 posMs)
{
    m_position = posMs;
    refreshCanSeekForward();
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
    refreshCanSeekForward();
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

void PlayerEngine::setPlaybackRateChangePending(bool pending)
{
    if (m_playbackRateChangePending == pending)
        return;
    m_playbackRateChangePending = pending;
    emit playbackRateChangePendingChanged(pending);
}

bool PlayerEngine::updateCanSeekForward()
{
    const bool available = isForwardSeekAvailable(
        m_mediaInfo != nullptr,
        m_position,
        m_duration,
        m_completion.isMediaFinished());
    if (m_canSeekForward == available)
        return false;

    m_canSeekForward = available;
    return true;
}

void PlayerEngine::refreshCanSeekForward()
{
    const bool changed = updateCanSeekForward();
    if (!changed)
        return;

    emit canSeekForwardChanged(m_canSeekForward);
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

    const bool canSeekForwardDidChange = updateCanSeekForward();
    m_pipeline->setPaused(true);
    setState(Paused);
    if (canSeekForwardDidChange)
        emit canSeekForwardChanged(m_canSeekForward);
    emit endOfMedia();
}
