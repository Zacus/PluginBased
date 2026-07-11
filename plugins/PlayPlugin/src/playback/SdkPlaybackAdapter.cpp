#include "playback/SdkPlaybackAdapter.h"

#include "Logger.h"

#include <QMetaObject>
#include <QThread>

extern "C" {
#include <libavutil/samplefmt.h>
}

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>
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

bool isObjectThread(const QObject& object)
{
    return QThread::currentThread() == object.thread();
}

} // namespace

class SdkPlaybackAdapter::SessionEventBridge final
    : public media_sdk::session::ISessionEvents
{
public:
    SessionEventBridge(SdkPlaybackAdapter& adapter, std::uint64_t eventSerial)
        : m_adapter(adapter)
        , m_eventSerial(eventSerial)
    {
    }

    void onEvent(const media_sdk::PlayerEvent& event) override
    {
        m_adapter.postSessionEvent(event, m_eventSerial);
    }

    void onRuntimeDiagnostics(media_sdk::runtime::RuntimeDiagnostics diagnostics) override
    {
        m_adapter.postRuntimeDiagnostics(diagnostics, m_eventSerial);
    }

    void onNativeRenderingFailed() override
    {
        m_adapter.postNativeRenderingFailed(m_eventSerial);
    }

private:
    SdkPlaybackAdapter& m_adapter;
    std::uint64_t m_eventSerial = 0;
};

namespace {

class RealSdkPlaybackSession final : public ISdkPlaybackSession {
public:
    RealSdkPlaybackSession(media_sdk::session::PlaybackSessionConfig config,
                           media_sdk::session::PlaybackSessionDependencies dependencies)
        : m_session(std::move(config), dependencies)
    {
    }

    media_sdk::Result<void> open(const std::filesystem::path& path) override
    {
        return m_session.open(path);
    }

    void play() override
    {
        m_session.play();
    }

    void pause() override
    {
        m_session.pause();
    }

    void stop() override
    {
        m_session.stop();
    }

    media_sdk::Result<void> seek(std::chrono::milliseconds position,
                                 media_sdk::SeekPlaybackMode mode) override
    {
        return m_session.seek(position, mode);
    }

    void setAudioControls(media_sdk::runtime::RuntimeAudioControls controls) override
    {
        m_session.setAudioControls(controls);
    }

    media_sdk::runtime::RuntimeTimeline timeline() const override
    {
        return m_session.timeline();
    }

private:
    media_sdk::session::PlaybackSession m_session;
};

SdkPlaybackSessionFactory defaultSessionFactory()
{
    return [](media_sdk::session::PlaybackSessionConfig config,
              media_sdk::session::PlaybackSessionDependencies dependencies) {
        return std::make_unique<RealSdkPlaybackSession>(std::move(config), dependencies);
    };
}

} // namespace

SdkPlaybackAdapter::SdkPlaybackAdapter(
    media_sdk::runtime::IAudioOutput* audioOutput,
    media_sdk::runtime::IVideoPresenter* videoPresenter,
    QObject* parent)
    : SdkPlaybackAdapter(audioOutput, videoPresenter, defaultSessionFactory(), parent)
{
}

SdkPlaybackAdapter::SdkPlaybackAdapter(
    media_sdk::runtime::IAudioOutput* audioOutput,
    media_sdk::runtime::IVideoPresenter* videoPresenter,
    SdkPlaybackSessionFactory sessionFactory,
    QObject* parent)
    : QObject(parent)
    , m_audioOutput(audioOutput)
    , m_videoPresenter(videoPresenter)
    , m_sessionFactory(std::move(sessionFactory))
{
}

SdkPlaybackAdapter::~SdkPlaybackAdapter()
{
    stopDecoding();
}

void SdkPlaybackAdapter::openFile(const QUrl& url)
{
    if (!isObjectThread(*this)) {
        QMetaObject::invokeMethod(this,
                                  [this, url]() {
                                      openFile(url);
                                  },
                                  Qt::QueuedConnection);
        return;
    }

    stopDecoding();

    std::unique_ptr<SessionEventBridge> eventBridge;
    std::uint64_t eventSerial = 0;
    {
        std::lock_guard lock(m_mutex);
        m_paused = false;
        eventSerial = ++m_eventSerial;
        eventBridge = std::make_unique<SessionEventBridge>(*this, eventSerial);
    }

    const auto dependencies = media_sdk::session::PlaybackSessionDependencies {
        .audioOutput = m_audioOutput,
        .videoPresenter = m_videoPresenter,
        .events = eventBridge.get(),
    };
    auto session = m_sessionFactory
        ? m_sessionFactory(sessionConfig(), dependencies)
        : nullptr;
    if (!session) {
        emit errorOccurred(QStringLiteral("SdkPlaybackAdapter session factory returned null"));
        return;
    }

    const auto path = pathFromUrl(url);
    const auto result = session->open(path);
    if (!result.ok()) {
        emit errorOccurred(QString::fromStdString(result.error().message));
        return;
    }

    {
        std::lock_guard lock(m_mutex);
        m_session = std::move(session);
        m_sessionEventBridge = std::move(eventBridge);
    }

    m_session->play();
}

void SdkPlaybackAdapter::setPaused(bool paused)
{
    if (!isObjectThread(*this)) {
        QMetaObject::invokeMethod(this,
                                  [this, paused]() {
                                      setPaused(paused);
                                  },
                                  Qt::QueuedConnection);
        return;
    }

    m_paused = paused;

    ISdkPlaybackSession* session = nullptr;
    {
        std::lock_guard lock(m_mutex);
        session = m_session.get();
    }
    if (!session)
        return;

    paused ? session->pause() : session->play();
}

void SdkPlaybackAdapter::seek(qint64 positionMs, int generation, bool resumeAfterSeek)
{
    if (!isObjectThread(*this)) {
        QMetaObject::invokeMethod(this,
                                  [this, positionMs, generation, resumeAfterSeek]() {
                                      seek(positionMs, generation, resumeAfterSeek);
                                  },
                                  Qt::QueuedConnection);
        return;
    }

    ISdkPlaybackSession* session = nullptr;
    {
        std::lock_guard lock(m_mutex);
        session = m_session.get();
        if (session) {
            m_pendingSeekRequests.push(std::chrono::milliseconds(positionMs), generation);
        }
    }
    if (!session)
        return;

    const auto mode = resumeAfterSeek
        ? media_sdk::SeekPlaybackMode::ResumePlayback
        : media_sdk::SeekPlaybackMode::PreservePlaybackState;
    const auto result = session->seek(std::chrono::milliseconds(positionMs), mode);
    if (!result.ok()) {
        {
            std::lock_guard lock(m_mutex);
            m_pendingSeekRequests.takeForCompletedPosition(std::chrono::milliseconds(positionMs));
        }
        emit errorOccurred(QString::fromStdString(result.error().message));
        return;
    }

    if (resumeAfterSeek) {
        std::lock_guard lock(m_mutex);
        m_paused = false;
    }
}

void SdkPlaybackAdapter::stopDecoding()
{
    if (!isObjectThread(*this)) {
        QMetaObject::invokeMethod(this,
                                  [this]() {
                                      stopDecoding();
                                  },
                                  Qt::BlockingQueuedConnection);
        return;
    }

    std::unique_ptr<SessionEventBridge> eventBridge;
    std::unique_ptr<ISdkPlaybackSession> session;
    {
        std::lock_guard lock(m_mutex);
        ++m_eventSerial;
        m_pendingSeekRequests.clear();
        session = std::move(m_session);
        eventBridge = std::move(m_sessionEventBridge);
    }

    if (session) {
        session->stop();
        session.reset();
    }
    eventBridge.reset();
}

void SdkPlaybackAdapter::setVideoToolboxDirectRenderingEnabled(bool enabled)
{
    if (!isObjectThread(*this)) {
        QMetaObject::invokeMethod(this,
                                  [this, enabled]() {
                                      setVideoToolboxDirectRenderingEnabled(enabled);
                                  },
                                  Qt::QueuedConnection);
        return;
    }

    std::lock_guard lock(m_mutex);
    m_directNativeVideoEnabled = enabled;
}

void SdkPlaybackAdapter::setVolume(float volume)
{
    if (!isObjectThread(*this)) {
        QMetaObject::invokeMethod(this,
                                  [this, volume]() {
                                      setVolume(volume);
                                  },
                                  Qt::QueuedConnection);
        return;
    }

    ISdkPlaybackSession* session = nullptr;
    media_sdk::runtime::RuntimeAudioControls controls;
    {
        std::lock_guard lock(m_mutex);
        m_volume = std::clamp(volume, 0.0f, 1.0f);
        controls = {
            .volume = m_volume,
            .muted = m_muted,
        };
        session = m_session.get();
    }
    if (session)
        session->setAudioControls(controls);
}

void SdkPlaybackAdapter::setMuted(bool muted)
{
    if (!isObjectThread(*this)) {
        QMetaObject::invokeMethod(this,
                                  [this, muted]() {
                                      setMuted(muted);
                                  },
                                  Qt::QueuedConnection);
        return;
    }

    ISdkPlaybackSession* session = nullptr;
    media_sdk::runtime::RuntimeAudioControls controls;
    {
        std::lock_guard lock(m_mutex);
        m_muted = muted;
        controls = {
            .volume = m_volume,
            .muted = m_muted,
        };
        session = m_session.get();
    }
    if (session)
        session->setAudioControls(controls);
}

void SdkPlaybackAdapter::postSessionEvent(const media_sdk::PlayerEvent& event,
                                          std::uint64_t eventSerial)
{
    auto eventCopy = std::make_shared<media_sdk::PlayerEvent>(event);
    QMetaObject::invokeMethod(this,
                              [this, eventCopy, eventSerial]() {
                                  handleSessionEvent(*eventCopy, eventSerial);
                              },
                              Qt::QueuedConnection);
}

void SdkPlaybackAdapter::postRuntimeDiagnostics(
    media_sdk::runtime::RuntimeDiagnostics diagnostics,
    std::uint64_t eventSerial)
{
    QMetaObject::invokeMethod(this,
                              [this, diagnostics, eventSerial]() {
                                  handleRuntimeDiagnostics(diagnostics, eventSerial);
                              },
                              Qt::QueuedConnection);
}

void SdkPlaybackAdapter::postNativeRenderingFailed(std::uint64_t eventSerial)
{
    QMetaObject::invokeMethod(this,
                              [this, eventSerial]() {
                                  handleNativeRenderingFailed(eventSerial);
                              },
                              Qt::QueuedConnection);
}

void SdkPlaybackAdapter::handleSessionEvent(
    media_sdk::PlayerEvent event,
    std::uint64_t eventSerial)
{
    if (!acceptsEventSerial(eventSerial))
        return;

    if (const auto* payload = std::get_if<media_sdk::MediaInfoEvent>(&event.payload)) {
        emit mediaInfoReady(toMilliseconds(payload->info.duration),
                            payload->info.width,
                            payload->info.height,
                            payload->info.fps,
                            payload->info.sampleRate,
                            payload->info.channels,
                            static_cast<quint64>(payload->info.channelLayoutMask),
                            static_cast<int>(AV_SAMPLE_FMT_FLT),
                            QString::fromStdString(payload->info.formatName));
        return;
    }

    if (const auto* payload = std::get_if<media_sdk::ErrorEvent>(&event.payload)) {
        emit errorOccurred(QString::fromStdString(payload->error.message));
        return;
    }

    if (const auto* payload = std::get_if<media_sdk::SeekCompletedEvent>(&event.payload)) {
        std::optional<int> qtGeneration;
        {
            std::lock_guard lock(m_mutex);
            qtGeneration = m_pendingSeekRequests.takeForCompletedPosition(payload->position);
        }
        if (!qtGeneration) {
            LOG_DEBUG("SdkPlaybackAdapter: ignored unmapped seek completion");
            return;
        }

        int serial = 0;
        {
            std::lock_guard lock(m_mutex);
            if (m_session)
                serial = static_cast<int>(m_session->timeline().generation);
        }
        emit seekCompleted(*qtGeneration, serial);
        return;
    }

    if (std::holds_alternative<media_sdk::EndOfFileEvent>(event.payload)) {
        emit endOfFile();
        emit endOfAudio();
        emit endOfVideo();
        return;
    }

    if (const auto* payload = std::get_if<media_sdk::PositionChangedEvent>(&event.payload)) {
        emit positionChanged(toMilliseconds(payload->position));
    }
}

void SdkPlaybackAdapter::handleRuntimeDiagnostics(
    media_sdk::runtime::RuntimeDiagnostics diagnostics,
    std::uint64_t eventSerial)
{
    if (!acceptsEventSerial(eventSerial))
        return;

    LOG_INFO("PlayPerf: sdk video={} native={} cpu={} fallback={} wait_us={} "
             "audio_bp={} video_bp={} audio_hw={} video_hw={} eof={}/{} abort={}",
             diagnostics.videoPresented,
             diagnostics.nativePresented,
             diagnostics.cpuPresented,
             diagnostics.nativeFallbacks,
             diagnostics.decodeFramePushWaitUs,
             diagnostics.audioBackpressureCount,
             diagnostics.videoBackpressureCount,
             diagnostics.audioQueueHighWatermark,
             diagnostics.videoQueueHighWatermark,
             diagnostics.eofAccepted,
             diagnostics.eofPresented,
             diagnostics.queueAbortCount);
}

void SdkPlaybackAdapter::handleNativeRenderingFailed(std::uint64_t eventSerial)
{
    if (!acceptsEventSerial(eventSerial))
        return;
    emit nativeRenderingFailed();
}

media_sdk::session::PlaybackSessionConfig SdkPlaybackAdapter::sessionConfig() const
{
    std::lock_guard lock(m_mutex);
    media_sdk::session::PlaybackSessionConfig config;
    config.core.preferNativeVideoFrames = m_directNativeVideoEnabled;
    config.preferNativeVideoFrames = m_directNativeVideoEnabled;
    config.runtime.audioControls = {
        .volume = m_volume,
        .muted = m_muted,
    };
    return config;
}

bool SdkPlaybackAdapter::acceptsEventSerial(std::uint64_t eventSerial) const
{
    std::lock_guard lock(m_mutex);
    return eventSerial == m_eventSerial && m_session;
}
