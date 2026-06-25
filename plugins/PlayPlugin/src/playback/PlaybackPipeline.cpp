#include "playback/PlaybackPipeline.h"

// 本文件实现播放管线的组件生命周期、信号转发、seek 协调和 Surface 绑定。
// 这些逻辑从 PlayerEngine 抽出后，可以单独演进而不扩大 QML API 类的职责。

#if defined(Q_OS_APPLE)
#include "media_sdk/platform/macos/CoreAudioAudioOutput.h"
#endif
#include "playback/QtRhiVideoPresenter.h"
#include "video/FFmpegSurface.h"
#include "Logger.h"

#include <chrono>

PlaybackPipeline::PlaybackPipeline(QObject* parent)
    : QObject(parent)
    , m_adapter(std::make_unique<QtPlaybackAdapter>(&m_videoQueue, &m_audioQueue))
    , m_audioRenderer(std::make_unique<AudioRenderer>(&m_audioQueue, &m_clock))
    , m_videoRenderer(std::make_unique<VideoRenderer>(&m_videoQueue, &m_clock))
{
    connect(m_adapter.get(), &QtPlaybackAdapter::mediaInfoReady,
            this, &PlaybackPipeline::mediaInfoReady);
    connect(m_adapter.get(), &QtPlaybackAdapter::errorOccurred,
            this, &PlaybackPipeline::errorOccurred);
    connect(m_adapter.get(), &QtPlaybackAdapter::endOfFile,
            this, &PlaybackPipeline::endOfFile);
    connect(m_adapter.get(), &QtPlaybackAdapter::positionChanged,
            this, &PlaybackPipeline::decoderPositionChanged);
    connect(m_adapter.get(), &QtPlaybackAdapter::seekCompleted,
            this, &PlaybackPipeline::onDecoderSeekCompleted);

    connect(m_audioRenderer.get(), &AudioRenderer::positionChanged,
            this, &PlaybackPipeline::audioPositionChanged);
    connect(m_audioRenderer.get(), &AudioRenderer::errorOccurred,
            this, &PlaybackPipeline::errorOccurred);
    connect(m_audioRenderer.get(), &AudioRenderer::endOfAudio,
            this, &PlaybackPipeline::endOfAudio);

    connect(m_videoRenderer.get(), &VideoRenderer::positionChanged,
            this, &PlaybackPipeline::videoPositionChanged);
    connect(m_videoRenderer.get(), &VideoRenderer::endOfVideo,
            this, &PlaybackPipeline::endOfVideo);
}

PlaybackPipeline::~PlaybackPipeline()
{
    stopComponents();
}

void PlaybackPipeline::setSurface(FFmpegSurface* surface)
{
    disconnectLegacySurface();

    m_surface = surface;

    if (m_runtimeMode == PlaybackRuntimeMode::LegacyQt) {
        connectLegacySurface();
    } else {
        destroySdkRuntimeChain();
        createSdkRuntimeChain();
    }

    updateNativeVideoRenderingEnabled();
}

void PlaybackPipeline::setRuntimeMode(PlaybackRuntimeMode mode)
{
    if (m_runtimeMode == mode)
        return;

    stopComponents();
    clearSurface();
    disconnectLegacySurface();
    destroySdkRuntimeChain();
    m_videoQueue.flush();
    m_audioQueue.flush();
    m_videoQueue.resetAbort();
    m_audioQueue.resetAbort();
    m_clock.invalidate();

    m_runtimeMode = mode;
    if (m_runtimeMode == PlaybackRuntimeMode::LegacyQt)
        connectLegacySurface();
    else
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
    if (m_runtimeMode == PlaybackRuntimeMode::SdkRuntime) {
        createSdkRuntimeChain();
        if (m_sdkAdapter)
            m_sdkAdapter->openFile(url);
        return;
    }

    m_audioRenderer->setAcceptedSerial(0);
    m_videoRenderer->setAcceptedSerial(0);

    m_videoQueue.resetAbort();
    m_audioQueue.resetAbort();
    m_clock.invalidate();

    updateNativeVideoRenderingEnabled();
    m_adapter->openFile(url);
}

void PlaybackPipeline::startRenderersForMedia(bool hasAudio,
                                              bool hasVideo,
                                              int sampleRate,
                                              int channels,
                                              quint64 channelLayoutMask,
                                              int sampleFmt)
{
    if (m_runtimeMode == PlaybackRuntimeMode::SdkRuntime) {
        createSdkRuntimeChain();
        return;
    }

    m_videoRenderer->setAudioClockEnabled(hasAudio);
    m_videoRenderer->setPaused(false);

    if (hasAudio) {
        m_audioRenderer->setSourceFormat(sampleRate,
                                         channels,
                                         static_cast<AVSampleFormat>(sampleFmt),
                                         channelLayoutMask);
        if (!m_audioRenderer->isRunning())
            m_audioRenderer->start(QThread::HighPriority);
    }

    if (hasVideo)
        m_videoRenderer->start();
}

void PlaybackPipeline::setPaused(bool paused)
{
    if (m_runtimeMode == PlaybackRuntimeMode::SdkRuntime) {
        if (m_sdkAdapter)
            m_sdkAdapter->setPaused(paused);
        return;
    }

    m_adapter->setPaused(paused);
    m_audioRenderer->setPaused(paused);
    m_videoRenderer->setPaused(paused);
}

void PlaybackPipeline::setVolume(float volume)
{
    m_audioRenderer->setVolume(volume);
}

void PlaybackPipeline::setMuted(bool muted)
{
    m_audioRenderer->setMuted(muted);
}

void PlaybackPipeline::stopComponents()
{
    m_adapter->stopDecoding();
    if (m_sdkAdapter)
        m_sdkAdapter->stopDecoding();

    m_audioRenderer->stopRenderer();
    m_audioRenderer->wait();

    m_videoRenderer->stop();
    m_videoRenderer->reset();

    m_videoQueue.flush();
    m_audioQueue.flush();
    m_videoQueue.resetAbort();
    m_audioQueue.resetAbort();
}

void PlaybackPipeline::seek(qint64 positionMs, int generation)
{
    if (m_runtimeMode == PlaybackRuntimeMode::SdkRuntime) {
        if (m_sdkAdapter)
            m_sdkAdapter->seek(positionMs, generation);
        return;
    }

    m_videoRenderer->beginSeek(generation);
    m_audioRenderer->setAcceptedSerial(generation);
    m_adapter->seekTo(positionMs, generation);
    m_audioRenderer->flush();
    m_videoRenderer->flush();
    m_clock.invalidate();
}

void PlaybackPipeline::onDecoderSeekCompleted(int generation, int serial)
{
    m_audioRenderer->setAcceptedSerial(serial);
    m_videoRenderer->completeSeek(generation, serial);
    emit seekCompleted(generation, serial);
}

void PlaybackPipeline::onNativeRenderingFailed()
{
    if (!m_nativeVideoRenderingEnabled)
        return;

    m_nativeVideoRenderingEnabled = false;
    if (m_runtimeMode == PlaybackRuntimeMode::SdkRuntime) {
        if (m_sdkAdapter)
            m_sdkAdapter->setVideoToolboxDirectRenderingEnabled(false);
    } else {
        m_adapter->setVideoToolboxDirectRenderingEnabled(false);
    }
    LOG_WARN("PlaybackPipeline: disabled VideoToolbox native rendering after Surface failure");
    emit nativeRenderingFailed();
}

void PlaybackPipeline::connectLegacySurface()
{
    if (!m_surface)
        return;

    connect(m_videoRenderer.get(), &VideoRenderer::frameReady,
            m_surface.data(), &FFmpegSurface::onFrameReady,
            Qt::DirectConnection);
    connect(m_surface.data(), &FFmpegSurface::nativeRenderingFailed,
            this, &PlaybackPipeline::onNativeRenderingFailed);
}

void PlaybackPipeline::disconnectLegacySurface()
{
    if (!m_surface)
        return;

    disconnect(m_videoRenderer.get(), &VideoRenderer::frameReady,
               m_surface.data(), &FFmpegSurface::onFrameReady);
    disconnect(m_surface.data(), &FFmpegSurface::nativeRenderingFailed,
               this, &PlaybackPipeline::onNativeRenderingFailed);
}

void PlaybackPipeline::createSdkRuntimeChain()
{
    if (m_sdkAdapter)
        return;

    disconnectLegacySurface();
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
            this, &PlaybackPipeline::seekCompleted);
    connect(m_sdkAdapter.get(), &SdkPlaybackAdapter::endOfAudio,
            this, &PlaybackPipeline::endOfAudio);
    connect(m_sdkAdapter.get(), &SdkPlaybackAdapter::endOfVideo,
            this, &PlaybackPipeline::endOfVideo);
    connect(m_sdkAdapter.get(), &SdkPlaybackAdapter::nativeRenderingFailed,
            this, &PlaybackPipeline::onNativeRenderingFailed);
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

    if (m_runtimeMode == PlaybackRuntimeMode::SdkRuntime) {
        if (m_nativeVideoRenderingEnabled == enabled) {
            if (m_sdkAdapter)
                m_sdkAdapter->setVideoToolboxDirectRenderingEnabled(enabled);
            return;
        }
        m_nativeVideoRenderingEnabled = enabled;
        if (m_sdkAdapter)
            m_sdkAdapter->setVideoToolboxDirectRenderingEnabled(enabled);
        m_adapter->setVideoToolboxDirectRenderingEnabled(false);
        LOG_INFO("PlaybackPipeline: SDK VideoToolbox native rendering {}",
                 enabled ? "enabled" : "disabled");
        return;
    }

    if (m_nativeVideoRenderingEnabled == enabled)
        return;

    m_nativeVideoRenderingEnabled = enabled;
    m_adapter->setVideoToolboxDirectRenderingEnabled(enabled);
    LOG_INFO("PlaybackPipeline: VideoToolbox native rendering {}",
             enabled ? "enabled" : "disabled");
}
