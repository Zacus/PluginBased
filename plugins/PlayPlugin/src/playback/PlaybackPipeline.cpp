#include "playback/PlaybackPipeline.h"

// 本文件实现播放管线的组件生命周期、信号转发、seek 协调和 Surface 绑定。
// 这些逻辑从 PlayerEngine 抽出后，可以单独演进而不扩大 QML API 类的职责。

#include "video/FFmpegSurface.h"
#include "Logger.h"

PlaybackPipeline::PlaybackPipeline(QObject* parent)
    : QObject(parent)
    , m_decoder(std::make_unique<FFmpegDecoder>(&m_videoQueue, &m_audioQueue))
    , m_audioRenderer(std::make_unique<AudioRenderer>(&m_audioQueue, &m_clock))
    , m_videoRenderer(std::make_unique<VideoRenderer>(&m_videoQueue, &m_clock))
    , m_seekCoordinator(*m_decoder, *m_audioRenderer, *m_videoRenderer, m_clock)
{
    connect(m_decoder.get(), &FFmpegDecoder::mediaInfoReady,
            this, &PlaybackPipeline::mediaInfoReady);
    connect(m_decoder.get(), &FFmpegDecoder::errorOccurred,
            this, &PlaybackPipeline::errorOccurred);
    connect(m_decoder.get(), &FFmpegDecoder::endOfFile,
            this, &PlaybackPipeline::endOfFile);
    connect(m_decoder.get(), &FFmpegDecoder::positionChanged,
            this, &PlaybackPipeline::decoderPositionChanged);
    connect(m_decoder.get(), &FFmpegDecoder::seekCompleted,
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
    if (m_surface) {
        disconnect(m_videoRenderer.get(), &VideoRenderer::frameReady,
                   m_surface.data(), &FFmpegSurface::onFrameReady);
        disconnect(m_surface.data(), &FFmpegSurface::nativeRenderingFailed,
                   this, &PlaybackPipeline::onNativeRenderingFailed);
    }

    m_surface = surface;

    if (m_surface) {
        connect(m_videoRenderer.get(), &VideoRenderer::frameReady,
                m_surface.data(), &FFmpegSurface::onFrameReady,
                Qt::DirectConnection);
        connect(m_surface.data(), &FFmpegSurface::nativeRenderingFailed,
                this, &PlaybackPipeline::onNativeRenderingFailed);
    }

    updateNativeVideoRenderingEnabled();
}

void PlaybackPipeline::clearSurface()
{
    if (m_surface)
        m_surface->clear();
}

void PlaybackPipeline::openFile(const QUrl& url)
{
    m_audioRenderer->setAcceptedSerial(0);
    m_videoRenderer->setAcceptedSerial(0);

    m_videoQueue.resetAbort();
    m_audioQueue.resetAbort();
    m_clock.invalidate();

    updateNativeVideoRenderingEnabled();
    m_decoder->openFile(url);
}

void PlaybackPipeline::startRenderersForMedia(bool hasAudio,
                                              bool hasVideo,
                                              int sampleRate,
                                              int channels,
                                              quint64 channelLayoutMask,
                                              int sampleFmt)
{
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
    m_decoder->setPaused(paused);
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
    m_decoder->stopDecoding();
    m_decoder->wait();

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
    m_seekCoordinator.seek(positionMs, generation);
}

void PlaybackPipeline::onDecoderSeekCompleted(int generation, int serial)
{
    m_seekCoordinator.complete(generation, serial);
    emit seekCompleted(generation, serial);
}

void PlaybackPipeline::onNativeRenderingFailed()
{
    if (!m_nativeVideoRenderingEnabled)
        return;

    m_nativeVideoRenderingEnabled = false;
    m_decoder->setVideoToolboxDirectRenderingEnabled(false);
    LOG_WARN("PlaybackPipeline: disabled VideoToolbox native rendering after Surface failure");
    emit nativeRenderingFailed();
}

void PlaybackPipeline::updateNativeVideoRenderingEnabled()
{
    const bool enabled = m_surface && m_surface->supportsNativeVideoToolboxRendering();
    if (m_nativeVideoRenderingEnabled == enabled)
        return;

    m_nativeVideoRenderingEnabled = enabled;
    m_decoder->setVideoToolboxDirectRenderingEnabled(enabled);
    LOG_INFO("PlaybackPipeline: VideoToolbox native rendering {}",
             enabled ? "enabled" : "disabled");
}
