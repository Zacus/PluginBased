#include "playback/QtPlaybackAdapter.h"

#include "Logger.h"

#include <QMetaObject>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <variant>

namespace {

std::filesystem::path pathFromUrl(const QUrl& url)
{
    return url.isLocalFile()
        ? std::filesystem::path(url.toLocalFile().toStdString())
        : std::filesystem::path(url.toString().toStdString());
}

qint64 toMilliseconds(std::chrono::milliseconds value)
{
    return static_cast<qint64>(value.count());
}

bool fullRange(media_sdk::ColorRange range)
{
    return range == media_sdk::ColorRange::Full;
}

bool bt709(media_sdk::ColorSpace colorSpace)
{
    return colorSpace == media_sdk::ColorSpace::Bt709;
}

int bytesPerSample(AVSampleFormat format)
{
    return std::max(0, av_get_bytes_per_sample(format));
}

int videoRowBytes(media_sdk::PixelFormat format, int width)
{
    switch (format)
    {
    case media_sdk::PixelFormat::Yuv420P:
    case media_sdk::PixelFormat::Nv12:
        return width;
    case media_sdk::PixelFormat::P010:
    case media_sdk::PixelFormat::Yuv420P10:
    case media_sdk::PixelFormat::Yuv422P10:
    case media_sdk::PixelFormat::Yuv444P10:
        return width * 2;
    default:
        return width;
    }
}

} // namespace

QtPlaybackAdapter::QtPlaybackAdapter(VideoFrameQueue* videoQueue,
                                     AudioFrameQueue* audioQueue,
                                     QObject* parent)
    : QObject(parent)
    , m_videoQueue(videoQueue)
    , m_audioQueue(audioQueue)
{
    m_config.preferNativeVideoFrames = false;
    resetPlayer();
}

QtPlaybackAdapter::~QtPlaybackAdapter()
{
    if (m_player)
        m_player->stop();
    m_player.reset();
}

void QtPlaybackAdapter::openFile(const QUrl& url)
{
    resetPlayer();
    m_currentSerial = 0;
    m_pendingSeekGeneration = 0;
    m_hasAudio = false;
    m_hasVideo = false;
    m_paused = false;
    if (m_videoQueue)
    {
        m_videoQueue->resetAbort();
        m_videoQueue->flush();
    }
    if (m_audioQueue)
    {
        m_audioQueue->resetAbort();
        m_audioQueue->flush();
    }

    const auto result = m_player->open(pathFromUrl(url));
    if (!result.ok())
    {
        emit errorOccurred(QString::fromStdString(result.error().message));
        return;
    }
    m_player->play();
}

void QtPlaybackAdapter::setPaused(bool paused)
{
    m_paused = paused;
    if (!m_player)
        return;
    paused ? m_player->pause() : m_player->play();
}

void QtPlaybackAdapter::seekTo(qint64 positionMs, int generation)
{
    if (!m_player)
        return;

    m_pendingSeekGeneration = generation;
    m_currentSerial = generation;
    if (m_videoQueue)
        m_videoQueue->flush();
    if (m_audioQueue)
        m_audioQueue->flush();

    const auto result = m_player->seek(std::chrono::milliseconds(positionMs));
    if (!result.ok())
    {
        emit errorOccurred(QString::fromStdString(result.error().message));
        return;
    }
    emit seekCompleted(m_pendingSeekGeneration, m_currentSerial);
}

void QtPlaybackAdapter::stopDecoding()
{
    if (m_player)
        m_player->stop();
}

void QtPlaybackAdapter::setVideoToolboxDirectRenderingEnabled(bool enabled)
{
    m_directNativeVideoEnabled = enabled;
    Q_UNUSED(m_directNativeVideoEnabled);
}

void QtPlaybackAdapter::onEvent(const media_sdk::PlayerEvent& event)
{
    auto eventCopy = std::make_shared<media_sdk::PlayerEvent>(event);
    QMetaObject::invokeMethod(this,
                              [this, eventCopy]() {
                                  handleEvent(*eventCopy);
                              },
                              Qt::QueuedConnection);
}

void QtPlaybackAdapter::handleEvent(const media_sdk::PlayerEvent& event)
{
    if (const auto* payload = std::get_if<media_sdk::MediaInfoEvent>(&event.payload))
    {
        const auto& info = payload->info;
        m_hasAudio = info.sampleRate > 0 && info.channels > 0;
        m_hasVideo = info.width > 0 && info.height > 0;
        emit mediaInfoReady(toMilliseconds(info.duration),
                            info.width,
                            info.height,
                            info.fps,
                            info.sampleRate,
                            info.channels,
                            static_cast<quint64>(info.channelLayoutMask),
                            AV_SAMPLE_FMT_FLT,
                            QString::fromStdString(info.formatName));
        return;
    }

    if (const auto* payload = std::get_if<media_sdk::ErrorEvent>(&event.payload))
    {
        emit errorOccurred(QString::fromStdString(payload->error.message));
        return;
    }

    if (std::holds_alternative<media_sdk::EndOfFileEvent>(event.payload))
    {
        if (m_videoQueue && m_hasVideo)
        {
            m_videoQueue->flush();
            m_videoQueue->tryPush(nullptr, m_currentSerial, true);
        }
        if (m_audioQueue && m_hasAudio)
        {
            m_audioQueue->flush();
            m_audioQueue->tryPush(nullptr, m_currentSerial, true);
        }
        emit endOfFile();
        return;
    }

    if (const auto* payload = std::get_if<media_sdk::PositionChangedEvent>(&event.payload))
    {
        emit positionChanged(toMilliseconds(payload->position));
        return;
    }

    if (const auto* payload = std::get_if<media_sdk::AudioFrameEvent>(&event.payload))
    {
        if (m_paused)
            return;
        if (m_audioQueue)
        {
            auto frame = makeAudioFrame(payload->frame);
            if (frame && !m_audioQueue->tryPush(std::move(frame), m_currentSerial))
                LOG_DEBUG("QtPlaybackAdapter: dropped audio frame because queue is full");
        }
        return;
    }

    if (const auto* payload = std::get_if<media_sdk::VideoFrameEvent>(&event.payload))
    {
        if (m_paused)
            return;
        if (m_videoQueue)
        {
            auto frame = makeVideoFrame(payload->frame);
            if (frame && !m_videoQueue->tryPush(std::move(frame), m_currentSerial))
                LOG_DEBUG("QtPlaybackAdapter: dropped video frame because queue is full");
        }
    }
}

void QtPlaybackAdapter::resetPlayer()
{
    if (m_player)
        m_player->stop();
    m_player = std::make_unique<media_sdk::Player>(m_config, *this);
}

AVFramePtr QtPlaybackAdapter::makeAudioFrame(const media_sdk::AudioFrame& frame) const
{
    auto avFrame = make_frame();
    const AVSampleFormat sourceFormat = mapSampleFormat(frame.sampleFormat());
    const int sampleBytes = bytesPerSample(sourceFormat);
    const int channels = std::max(1, frame.channels());
    const int nbSamples = sampleBytes > 0
        ? static_cast<int>(frame.samples().size() / static_cast<std::size_t>(sampleBytes * channels))
        : 0;

    avFrame->format = AV_SAMPLE_FMT_FLT;
    avFrame->sample_rate = frame.sampleRate();
    avFrame->nb_samples = nbSamples;
    avFrame->pts = frame.pts().count();
    av_channel_layout_default(&avFrame->ch_layout, channels);
    if (av_frame_get_buffer(avFrame.get(), 0) < 0)
        return {};

    const auto samples = frame.samples();
    auto* out = reinterpret_cast<float*>(avFrame->data[0]);
    if (!samples.empty() && out)
    {
        const int sampleCount = nbSamples * channels;
        if (sourceFormat == AV_SAMPLE_FMT_FLT)
        {
            std::memcpy(out, samples.data(), static_cast<std::size_t>(sampleCount) * sizeof(float));
        }
        else if (sourceFormat == AV_SAMPLE_FMT_S32)
        {
            const auto* in = reinterpret_cast<const std::int32_t*>(samples.data());
            for (int i = 0; i < sampleCount; ++i)
                out[i] = static_cast<float>(in[i]) / 2147483648.0f;
        }
        else
        {
            const auto* in = reinterpret_cast<const std::int16_t*>(samples.data());
            for (int i = 0; i < sampleCount; ++i)
                out[i] = static_cast<float>(in[i]) / 32768.0f;
        }
    }
    return avFrame;
}

AVFramePtr QtPlaybackAdapter::makeVideoFrame(const media_sdk::VideoFrame& frame) const
{
    if (frame.pixelFormat() == media_sdk::PixelFormat::Native)
        return {};

    auto avFrame = make_frame();
    avFrame->format = mapPixelFormat(frame.pixelFormat());
    avFrame->width = frame.width();
    avFrame->height = frame.height();
    avFrame->pts = frame.pts().count();
    applyColorMetadata(frame, avFrame.get());
    if (av_frame_get_buffer(avFrame.get(), 32) < 0)
        return {};
    if (av_frame_make_writable(avFrame.get()) < 0)
        return {};

    const auto planes = frame.planes();
    for (std::size_t i = 0; i < planes.size() && i < AV_NUM_DATA_POINTERS; ++i)
    {
        const auto& plane = planes[i];
        if (!plane.data || !avFrame->data[i])
            continue;

        const int rowBytes = std::min({
            std::abs(plane.stride),
            std::abs(avFrame->linesize[i]),
            videoRowBytes(frame.pixelFormat(), plane.width)
        });
        for (int row = 0; row < plane.height; ++row)
        {
            const auto* src = reinterpret_cast<const std::uint8_t*>(plane.data)
                + row * plane.stride;
            auto* dst = avFrame->data[i] + row * avFrame->linesize[i];
            std::memcpy(dst, src, static_cast<std::size_t>(rowBytes));
        }
    }

    return avFrame;
}

AVPixelFormat QtPlaybackAdapter::mapPixelFormat(media_sdk::PixelFormat format) const
{
    switch (format)
    {
    case media_sdk::PixelFormat::Yuv420P:
        return AV_PIX_FMT_YUV420P;
    case media_sdk::PixelFormat::Nv12:
        return AV_PIX_FMT_NV12;
    case media_sdk::PixelFormat::P010:
        return AV_PIX_FMT_P010LE;
    case media_sdk::PixelFormat::Yuv420P10:
        return AV_PIX_FMT_YUV420P10LE;
    case media_sdk::PixelFormat::Yuv422P10:
        return AV_PIX_FMT_YUV422P10LE;
    case media_sdk::PixelFormat::Yuv444P10:
        return AV_PIX_FMT_YUV444P10LE;
    default:
        return AV_PIX_FMT_YUV420P;
    }
}

AVSampleFormat QtPlaybackAdapter::mapSampleFormat(media_sdk::AudioSampleFormat format) const
{
    switch (format)
    {
    case media_sdk::AudioSampleFormat::Float32Interleaved:
        return AV_SAMPLE_FMT_FLT;
    case media_sdk::AudioSampleFormat::Signed32Interleaved:
        return AV_SAMPLE_FMT_S32;
    case media_sdk::AudioSampleFormat::Signed16Interleaved:
    default:
        return AV_SAMPLE_FMT_S16;
    }
}

void QtPlaybackAdapter::applyColorMetadata(const media_sdk::VideoFrame& source,
                                           AVFrame* destination) const
{
    destination->color_range = fullRange(source.colorRange())
        ? AVCOL_RANGE_JPEG
        : AVCOL_RANGE_MPEG;
    destination->colorspace = bt709(source.colorSpace())
        ? AVCOL_SPC_BT709
        : AVCOL_SPC_BT470BG;
}
