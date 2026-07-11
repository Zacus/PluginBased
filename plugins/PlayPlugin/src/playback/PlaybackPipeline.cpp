#include "playback/PlaybackPipeline.h"

// PlaybackPipeline keeps Qt-specific presentation objects in PlayPlugin and
// delegates playback orchestration, A/V sync, queues, audio output, and EOF
// semantics to media_sdk::session::PlaybackSession through SdkPlaybackAdapter.

#if defined(Q_OS_APPLE)
#include "media_sdk/platform/macos/CoreAudioAudioOutput.h"
#endif
#include "playback/QtRhiVideoPresenter.h"
#include "video/FFmpegSurface.h"
#include "Logger.h"

PlaybackPipeline::PlaybackPipeline(QObject* parent)
    : QObject(parent)
{
}

PlaybackPipeline::~PlaybackPipeline()
{
    stopComponents();
}

void PlaybackPipeline::setSurface(FFmpegSurface* surface)
{
    if (m_surface) {
        disconnect(m_surface.data(), &FFmpegSurface::nativeRenderingFailed,
                   this, &PlaybackPipeline::onSurfaceNativeRenderingFailed);
    }
    destroySdkRuntimeChain();
    m_surface = surface;
    createSdkRuntimeChain();
    updateNativeVideoRenderingEnabled();
}

void PlaybackPipeline::clearSurface()
{
    if (m_surface)
        m_surface->clear();
}

void PlaybackPipeline::openFile(const QUrl& url)
{
    createSdkRuntimeChain();
    if (m_sdkAdapter)
        m_sdkAdapter->openFile(url);
}

void PlaybackPipeline::startRenderersForMedia(bool hasAudio,
                                              bool hasVideo,
                                              int sampleRate,
                                              int channels,
                                              quint64 channelLayoutMask,
                                              int sampleFmt)
{
    Q_UNUSED(hasAudio);
    Q_UNUSED(hasVideo);
    Q_UNUSED(sampleRate);
    Q_UNUSED(channels);
    Q_UNUSED(channelLayoutMask);
    Q_UNUSED(sampleFmt);
    createSdkRuntimeChain();
}

void PlaybackPipeline::setPaused(bool paused)
{
    if (m_sdkAdapter)
        m_sdkAdapter->setPaused(paused);
}

void PlaybackPipeline::setVolume(float volume)
{
    if (m_sdkAdapter)
        m_sdkAdapter->setVolume(volume);
}

void PlaybackPipeline::setMuted(bool muted)
{
    if (m_sdkAdapter)
        m_sdkAdapter->setMuted(muted);
}

void PlaybackPipeline::stopComponents()
{
    if (m_sdkAdapter)
        m_sdkAdapter->stopDecoding();
}

void PlaybackPipeline::seek(qint64 positionMs, int generation, bool resumeAfterSeek)
{
    if (m_sdkAdapter)
        m_sdkAdapter->seek(positionMs, generation, resumeAfterSeek);
}

void PlaybackPipeline::onDecoderSeekCompleted(int generation, int serial)
{
    emit seekCompleted(generation, serial);
}

void PlaybackPipeline::onSurfaceNativeRenderingFailed()
{
    disableNativeVideoRenderingAfterFailure(true);
}

void PlaybackPipeline::onSdkNativeRenderingFailed()
{
    disableNativeVideoRenderingAfterFailure(false);
}

void PlaybackPipeline::disableNativeVideoRenderingAfterFailure(bool notifySdkSession)
{
    if (!m_nativeVideoRenderingEnabled)
        return;

    m_nativeVideoRenderingEnabled = false;
    if (notifySdkSession && m_sdkAdapter)
        m_sdkAdapter->notifyNativeRenderingFailed();
    LOG_WARN("PlaybackPipeline: disabled VideoToolbox native rendering after native render failure");
    emit nativeRenderingFailed();
}

void PlaybackPipeline::createSdkRuntimeChain()
{
    if (m_sdkAdapter)
        return;

    if (!m_surface)
        return;

#if defined(Q_OS_APPLE)
    m_sdkAudioOutput = std::make_unique<media_sdk::platform::macos::CoreAudioAudioOutput>();
    m_sdkVideoPresenter = std::make_unique<QtRhiVideoPresenter>(m_surface.data());
    m_sdkAdapter = std::make_unique<SdkPlaybackAdapter>(
        m_sdkAudioOutput.get(),
        m_sdkVideoPresenter.get(),
        this);
    connect(m_sdkAdapter.get(), &SdkPlaybackAdapter::mediaInfoReady,
            this, &PlaybackPipeline::mediaInfoReady);
    connect(m_sdkAdapter.get(), &SdkPlaybackAdapter::errorOccurred,
            this, &PlaybackPipeline::errorOccurred);
    connect(m_sdkAdapter.get(), &SdkPlaybackAdapter::endOfFile,
            this, &PlaybackPipeline::endOfFile);
    connect(m_sdkAdapter.get(), &SdkPlaybackAdapter::positionChanged,
            this, &PlaybackPipeline::decoderPositionChanged);
    connect(m_sdkAdapter.get(), &SdkPlaybackAdapter::seekCompleted,
            this, &PlaybackPipeline::onDecoderSeekCompleted);
    connect(m_sdkAdapter.get(), &SdkPlaybackAdapter::endOfAudio,
            this, &PlaybackPipeline::endOfAudio);
    connect(m_sdkAdapter.get(), &SdkPlaybackAdapter::endOfVideo,
            this, &PlaybackPipeline::endOfVideo);
    connect(m_sdkAdapter.get(), &SdkPlaybackAdapter::nativeRenderingFailed,
            this, &PlaybackPipeline::onSdkNativeRenderingFailed);
    connect(m_surface.data(), &FFmpegSurface::nativeRenderingFailed,
            this, &PlaybackPipeline::onSurfaceNativeRenderingFailed,
            Qt::UniqueConnection);
#else
    LOG_WARN("PlaybackPipeline: SDK runtime mode requires a platform audio output");
#endif
}

void PlaybackPipeline::destroySdkRuntimeChain()
{
    if (m_sdkAdapter)
        m_sdkAdapter->stopDecoding();
    m_sdkAdapter.reset();
    m_sdkVideoPresenter.reset();
#if defined(Q_OS_APPLE)
    m_sdkAudioOutput.reset();
#endif
}

void PlaybackPipeline::updateNativeVideoRenderingEnabled()
{
    const bool enabled = m_surface && m_surface->supportsNativeVideoToolboxRendering();
    if (m_nativeVideoRenderingEnabled == enabled) {
        if (m_sdkAdapter)
            m_sdkAdapter->setVideoToolboxDirectRenderingEnabled(enabled);
        return;
    }

    m_nativeVideoRenderingEnabled = enabled;
    if (m_sdkAdapter)
        m_sdkAdapter->setVideoToolboxDirectRenderingEnabled(enabled);
    LOG_INFO("PlaybackPipeline: SDK VideoToolbox native rendering {}",
             enabled ? "enabled" : "disabled");
}
