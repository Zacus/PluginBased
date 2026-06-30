#include "playback/SdkPlaybackAdapter.h"

#include "Logger.h"

#include <QMetaObject>

extern "C" {
#include <libavutil/samplefmt.h>
}

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <variant>
#include <vector>

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

std::chrono::milliseconds toMilliseconds(std::chrono::microseconds value)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(value);
}

media_sdk::runtime::AudioSampleFormat runtimeAudioFormat(media_sdk::AudioSampleFormat format)
{
    switch (format) {
    case media_sdk::AudioSampleFormat::Float32Interleaved:
        return media_sdk::runtime::AudioSampleFormat::Float32;
    default:
        return media_sdk::runtime::AudioSampleFormat::Float32;
    }
}

std::vector<std::byte> float32InterleavedSamples(const media_sdk::AudioFrame& frame)
{
    const auto samples = frame.samples();
    if (frame.sampleFormat() == media_sdk::AudioSampleFormat::Float32Interleaved)
        return { samples.begin(), samples.end() };

    const int channels = std::max(1, frame.channels());
    std::size_t sampleCount = 0;
    std::vector<float> converted;

    if (frame.sampleFormat() == media_sdk::AudioSampleFormat::Signed16Interleaved) {
        sampleCount = samples.size() / sizeof(std::int16_t);
        converted.resize(sampleCount);
        const auto* input = reinterpret_cast<const std::int16_t*>(samples.data());
        for (std::size_t i = 0; i < sampleCount; ++i)
            converted[i] = static_cast<float>(input[i]) / 32768.0f;
    } else if (frame.sampleFormat() == media_sdk::AudioSampleFormat::Signed32Interleaved) {
        sampleCount = samples.size() / sizeof(std::int32_t);
        converted.resize(sampleCount);
        const auto* input = reinterpret_cast<const std::int32_t*>(samples.data());
        for (std::size_t i = 0; i < sampleCount; ++i)
            converted[i] = static_cast<float>(input[i]) / 2147483648.0f;
    } else {
        converted.resize(static_cast<std::size_t>(channels), 0.0f);
    }

    std::vector<std::byte> output(converted.size() * sizeof(float));
    if (!output.empty())
        std::memcpy(output.data(), converted.data(), output.size());
    return output;
}

} // namespace

SdkPlaybackAdapter::SdkPlaybackAdapter(
    media_sdk::runtime::IAudioOutput* audioOutput,
    media_sdk::runtime::IVideoPresenter* videoPresenter,
    QObject* parent)
    : QObject(parent)
    , m_audioOutput(audioOutput)
    , m_videoPresenter(videoPresenter)
{
    m_config.preferNativeVideoFrames = true;
    resetPlayer(true);
}

SdkPlaybackAdapter::~SdkPlaybackAdapter()
{
    stopDecoding();
}

void SdkPlaybackAdapter::openFile(const QUrl& url)
{
    stopDecoding();
    m_currentPath = pathFromUrl(url);
    resetPlayer(m_directNativeVideoEnabled);
    if (!openCorePlayer(m_currentPath))
        return;
    m_player->play();
    if (m_paused)
        m_player->pause();
}

void SdkPlaybackAdapter::setPaused(bool paused)
{
    m_paused = paused;
    std::shared_ptr<media_sdk::runtime::RuntimePlayer> runtimePlayer;
    {
        std::lock_guard lock(m_mutex);
        runtimePlayer = m_runtimePlayer;
    }
    if (runtimePlayer) {
        paused ? runtimePlayer->pause() : runtimePlayer->resume();
    }
    if (m_player)
        paused ? m_player->pause() : m_player->play();
}

void SdkPlaybackAdapter::seek(qint64 positionMs, int generation)
{
    if (!m_player)
        return;

    std::shared_ptr<media_sdk::runtime::RuntimePlayer> runtimePlayer;
    {
        std::lock_guard lock(m_mutex);
        m_acceptingRuntimeFrames = false;
        runtimePlayer = m_runtimePlayer;
    }

    media_sdk::runtime::RuntimeTimeline runtimeTimeline;
    if (runtimePlayer) {
        runtimePlayer->seek(std::chrono::milliseconds(positionMs));
        runtimeTimeline = runtimePlayer->timeline();
    }

    {
        std::lock_guard lock(m_mutex);
        m_pendingSeekRequests.push(std::chrono::milliseconds(positionMs),
                                   PendingSeekRequest { generation, runtimeTimeline });
        if (m_runtimePlayer == runtimePlayer)
            m_runtimeTimeline = runtimeTimeline;
    }

    const auto result = m_player->seek(std::chrono::milliseconds(positionMs));
    if (!result.ok())
        emit errorOccurred(QString::fromStdString(result.error().message));
}

void SdkPlaybackAdapter::stopDecoding()
{
    std::shared_ptr<media_sdk::runtime::RuntimePlayer> runtimePlayer;
    {
        std::lock_guard lock(m_mutex);
        runtimePlayer = std::move(m_runtimePlayer);
        m_acceptingRuntimeFrames = false;
        m_pendingFallback.reset();
        m_pendingSeekRequests.clear();
        m_acceptedCoreTimeline = {};
        m_runtimeTimeline = {};
    }
    if (runtimePlayer)
        runtimePlayer->stop();

    if (m_player)
        m_player->stop();
    m_player.reset();
}

void SdkPlaybackAdapter::setVideoToolboxDirectRenderingEnabled(bool enabled)
{
    m_directNativeVideoEnabled = enabled;
}

void SdkPlaybackAdapter::onEvent(const media_sdk::PlayerEvent& event)
{
    const auto seekCompletion = acceptSeekCompletedEvent(event);
    if (handleDataEvent(event))
        return;

    auto eventCopy = std::make_shared<media_sdk::PlayerEvent>(event);
    QMetaObject::invokeMethod(this,
                              [this, eventCopy, seekCompletion]() {
                                  handleControlEvent(*eventCopy, seekCompletion);
                              },
                              Qt::QueuedConnection);
}

void SdkPlaybackAdapter::onFallbackToCpuRequested(
    media_sdk::runtime::RuntimeFallbackAction action)
{
    QMetaObject::invokeMethod(this,
                              [this, action]() {
                                  handleFallbackOnObjectThread(action);
                              },
                              Qt::QueuedConnection);
}

void SdkPlaybackAdapter::onEndOfStreamPresented(
    media_sdk::runtime::RuntimeTimeline timeline)
{
    QMetaObject::invokeMethod(this,
                              [this, timeline]() {
                                  Q_UNUSED(timeline);
                                  emit endOfAudio();
                                  emit endOfVideo();
                              },
                              Qt::QueuedConnection);
}

bool SdkPlaybackAdapter::handleDataEvent(const media_sdk::PlayerEvent& event)
{
    std::shared_ptr<media_sdk::runtime::RuntimePlayer> runtimePlayer;
    media_sdk::runtime::RuntimeTimeline runtimeTimeline;
    {
        std::lock_guard lock(m_mutex);
        if (!m_acceptingRuntimeFrames || !m_runtimePlayer || !acceptsCoreEvent(event.metadata))
            return std::holds_alternative<media_sdk::AudioFrameEvent>(event.payload)
                || std::holds_alternative<media_sdk::VideoFrameEvent>(event.payload);
        runtimePlayer = m_runtimePlayer;
        runtimeTimeline = m_runtimeTimeline;
    }

    if (const auto* payload = std::get_if<media_sdk::AudioFrameEvent>(&event.payload)) {
        runtimePlayer->enqueueAudio(runtimeAudioFrame(payload->frame, runtimeTimeline));
        return true;
    }

    if (auto* payload = std::get_if<media_sdk::VideoFrameEvent>(&event.payload)) {
        runtimePlayer->enqueueVideo(runtimeVideoFrame(std::move(payload->frame), runtimeTimeline));
        return true;
    }

    return false;
}

void SdkPlaybackAdapter::handleControlEvent(
    const media_sdk::PlayerEvent& event,
    std::optional<AcceptedSeekCompletion> acceptedSeekCompletion)
{
    if (const auto* payload = std::get_if<media_sdk::MediaInfoEvent>(&event.payload)) {
        if (!m_pendingFallback && !ensureRuntimeForMedia(payload->info, m_directNativeVideoEnabled))
            return;
        if (!m_pendingFallback) {
            setAcceptedCoreTimeline(event.metadata, currentTimeline());
            emit mediaInfoReady(toMilliseconds(payload->info.duration),
                                payload->info.width,
                                payload->info.height,
                                payload->info.fps,
                                payload->info.sampleRate,
                                payload->info.channels,
                                static_cast<quint64>(payload->info.channelLayoutMask),
                                static_cast<int>(AV_SAMPLE_FMT_FLT),
                                QString::fromStdString(payload->info.formatName));
        }
        return;
    }

    if (const auto* payload = std::get_if<media_sdk::ErrorEvent>(&event.payload)) {
        emit errorOccurred(QString::fromStdString(payload->error.message));
        return;
    }

    if (std::holds_alternative<media_sdk::SeekCompletedEvent>(event.payload)) {
        if (acceptedSeekCompletion && acceptedSeekCompletion->hasQtGeneration) {
            emit seekCompleted(
                acceptedSeekCompletion->qtGeneration,
                static_cast<int>(acceptedSeekCompletion->runtimeTimeline.generation));
        }
        return;
    }

    if (std::holds_alternative<media_sdk::EndOfFileEvent>(event.payload)) {
        std::shared_ptr<media_sdk::runtime::RuntimePlayer> runtimePlayer;
        media_sdk::runtime::RuntimeTimeline runtimeTimeline;
        {
            std::lock_guard lock(m_mutex);
            runtimePlayer = m_runtimePlayer;
            runtimeTimeline = m_runtimeTimeline;
        }
        if (runtimePlayer)
            runtimePlayer->enqueueEndOfStream(runtimeTimeline.sessionId, runtimeTimeline.generation);
        emit endOfFile();
        return;
    }

    if (const auto* payload = std::get_if<media_sdk::PositionChangedEvent>(&event.payload)) {
        emit positionChanged(toMilliseconds(payload->position));
        return;
    }
}

std::optional<SdkPlaybackAdapter::AcceptedSeekCompletion>
SdkPlaybackAdapter::acceptSeekCompletedEvent(const media_sdk::PlayerEvent& event)
{
    const auto* payload = std::get_if<media_sdk::SeekCompletedEvent>(&event.payload);
    if (!payload)
        return std::nullopt;

    std::shared_ptr<media_sdk::runtime::RuntimePlayer> runtimePlayer;
    AcceptedSeekCompletion completion;
    bool completeFallbackSeek = false;
    {
        std::lock_guard lock(m_mutex);
        media_sdk::runtime::RuntimeTimeline runtimeTimeline = m_runtimeTimeline;

        if (m_pendingFallback) {
            runtimeTimeline = {
                .sessionId = m_pendingFallback->sessionId,
                .generation = m_pendingFallback->generation,
            };
            runtimePlayer = m_runtimePlayer;
            completeFallbackSeek = true;
            m_pendingFallback.reset();
        } else if (auto pending = m_pendingSeekRequests.takeForCompletedPosition(payload->position)) {
            runtimeTimeline = pending->runtimeTimeline;
            completion.qtGeneration = pending->qtGeneration;
            completion.hasQtGeneration = true;
        }

        m_acceptedCoreTimeline = event.metadata;
        m_runtimeTimeline = runtimeTimeline;
        m_acceptingRuntimeFrames = true;
        completion.runtimeTimeline = runtimeTimeline;
    }

    if (completeFallbackSeek && runtimePlayer)
        runtimePlayer->completeSeek(completion.runtimeTimeline.sessionId,
                                    completion.runtimeTimeline.generation);

    return completion;
}

void SdkPlaybackAdapter::handleFallbackOnObjectThread(
    media_sdk::runtime::RuntimeFallbackAction action)
{
    LOG_WARN("SdkPlaybackAdapter: native presenter failed, switching current session to CPU decode");
    m_pendingFallback = action;
    m_config.preferNativeVideoFrames = false;
    resetPlayer(false);
    if (!openCorePlayer(m_currentPath))
        return;
    m_player->play();
    const auto result = m_player->seek(toMilliseconds(action.resumePosition));
    if (!result.ok())
        emit errorOccurred(QString::fromStdString(result.error().message));
    emit nativeRenderingFailed();
}

void SdkPlaybackAdapter::resetPlayer(bool preferNativeVideoFrames)
{
    if (m_player)
        m_player->stop();
    m_config.preferNativeVideoFrames = preferNativeVideoFrames;
    m_player = std::make_unique<media_sdk::Player>(m_config, *this);
}

bool SdkPlaybackAdapter::openCorePlayer(const std::filesystem::path& path)
{
    if (path.empty()) {
        emit errorOccurred(QStringLiteral("Cannot open an empty media path"));
        return false;
    }
    const auto result = m_player->open(path);
    if (!result.ok()) {
        emit errorOccurred(QString::fromStdString(result.error().message));
        return false;
    }
    return true;
}

bool SdkPlaybackAdapter::ensureRuntimeForMedia(
    const media_sdk::MediaInfo& info,
    bool preferNativeVideoFrames)
{
    if (!m_audioOutput || !m_videoPresenter) {
        emit errorOccurred(QStringLiteral("SDK runtime requires audio output and video presenter"));
        return false;
    }

    media_sdk::runtime::RuntimePlayerConfig config;
    const bool hasAudio = info.sampleRate > 0 && info.channels > 0;
    config.audioFormat = {
        .sampleRate = hasAudio ? info.sampleRate : 48000,
        .channels = hasAudio ? info.channels : 2,
        .sampleFormat = runtimeAudioFormat(media_sdk::AudioSampleFormat::Float32Interleaved),
    };
    config.outputPolicy = preferNativeVideoFrames
        ? media_sdk::runtime::VideoOutputPolicy::PreferNative
        : media_sdk::runtime::VideoOutputPolicy::CpuOnly;
    config.audioClockEnabled = hasAudio;

    auto runtimePlayer = std::make_shared<media_sdk::runtime::RuntimePlayer>(
        config,
        media_sdk::runtime::RuntimePlayerDependencies {
            .audioOutput = m_audioOutput,
            .videoPresenter = m_videoPresenter,
            .events = this,
        });
    const auto openResult = runtimePlayer->open();
    if (!openResult.ok()) {
        emit errorOccurred(QString::fromStdString(openResult.error().message));
        return false;
    }

    const auto runtimeTimeline = runtimePlayer->timeline();
    std::lock_guard lock(m_mutex);
    m_runtimePlayer = std::move(runtimePlayer);
    m_runtimeTimeline = runtimeTimeline;
    return true;
}

media_sdk::runtime::RuntimeTimeline SdkPlaybackAdapter::currentTimeline() const
{
    std::shared_ptr<media_sdk::runtime::RuntimePlayer> runtimePlayer;
    media_sdk::runtime::RuntimeTimeline timeline;
    {
        std::lock_guard lock(m_mutex);
        runtimePlayer = m_runtimePlayer;
        timeline = m_runtimeTimeline;
    }
    return runtimePlayer ? runtimePlayer->timeline() : timeline;
}

media_sdk::runtime::RuntimeAudioFrame SdkPlaybackAdapter::runtimeAudioFrame(
    const media_sdk::AudioFrame& frame,
    media_sdk::runtime::RuntimeTimeline timeline) const
{
    const auto samples = float32InterleavedSamples(frame);
    media_sdk::AudioFrame converted(media_sdk::AudioFrameDesc {
        .sampleFormat = media_sdk::AudioSampleFormat::Float32Interleaved,
        .sampleRate = frame.sampleRate(),
        .channels = frame.channels(),
        .pts = frame.pts(),
        .samples = samples,
    });
    return {
        .frame = std::move(converted),
        .sessionId = timeline.sessionId,
        .generation = timeline.generation,
    };
}

media_sdk::runtime::RuntimeVideoFrame SdkPlaybackAdapter::runtimeVideoFrame(
    media_sdk::VideoFrame frame,
    media_sdk::runtime::RuntimeTimeline timeline) const
{
    return {
        .frame = std::move(frame),
        .sessionId = timeline.sessionId,
        .generation = timeline.generation,
    };
}

bool SdkPlaybackAdapter::acceptsCoreEvent(const media_sdk::EventMetadata& metadata) const
{
    return metadata.sessionId == m_acceptedCoreTimeline.sessionId
        && metadata.generation == m_acceptedCoreTimeline.generation;
}

void SdkPlaybackAdapter::setAcceptedCoreTimeline(
    const media_sdk::EventMetadata& metadata,
    media_sdk::runtime::RuntimeTimeline runtimeTimeline)
{
    std::lock_guard lock(m_mutex);
    m_acceptedCoreTimeline = metadata;
    m_runtimeTimeline = runtimeTimeline;
    m_acceptingRuntimeFrames = true;
}
