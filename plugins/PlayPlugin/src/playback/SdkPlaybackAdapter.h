#pragma once

#include "media_sdk/session/PlaybackSession.h"
#include "playback/PendingSeekRequests.h"

#include <QObject>
#include <QUrl>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>

class ISdkPlaybackSession {
public:
    virtual ~ISdkPlaybackSession() = default;

    [[nodiscard("open result determines whether adapter may start playback")]]
    virtual media_sdk::Result<void> open(const std::filesystem::path& path) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    [[nodiscard("seek result determines whether adapter should keep pending seek state")]]
    virtual media_sdk::Result<void> seek(std::chrono::milliseconds position,
                                         media_sdk::SeekPlaybackMode mode) = 0;
    [[nodiscard("rate result determines whether adapter should await confirmation")]]
    virtual media_sdk::Result<void> setPlaybackRate(double playbackRate) = 0;
    [[nodiscard]] virtual double playbackRate() const = 0;
    virtual void setAudioControls(media_sdk::runtime::RuntimeAudioControls controls) = 0;
    virtual void notifyNativeRenderingFailed() = 0;
    [[nodiscard("timeline maps SDK generation back to Qt seek serials")]]
    virtual media_sdk::runtime::RuntimeTimeline timeline() const = 0;
};

using SdkPlaybackSessionFactory = std::function<std::unique_ptr<ISdkPlaybackSession>(
    media_sdk::session::PlaybackSessionConfig,
    media_sdk::session::PlaybackSessionDependencies)>;

class SdkPlaybackAdapter final : public QObject
{
    Q_OBJECT

public:
    SdkPlaybackAdapter(media_sdk::runtime::IAudioOutput* audioOutput,
                       media_sdk::runtime::IVideoPresenter* videoPresenter,
                       media_sdk::runtime::IAudioTempoProcessor* audioTempoProcessor,
                       QObject* parent = nullptr);
    SdkPlaybackAdapter(media_sdk::runtime::IAudioOutput* audioOutput,
                       media_sdk::runtime::IVideoPresenter* videoPresenter,
                       media_sdk::runtime::IAudioTempoProcessor* audioTempoProcessor,
                       SdkPlaybackSessionFactory sessionFactory,
                       QObject* parent = nullptr);
    ~SdkPlaybackAdapter() override;

    void openFile(const QUrl& url);
    void setPaused(bool paused);
    void seek(qint64 positionMs, int generation, bool resumeAfterSeek = false);
    void stopDecoding();
    void setVideoToolboxDirectRenderingEnabled(bool enabled);
    void notifyNativeRenderingFailed();
    void setVolume(float volume);
    void setMuted(bool muted);
    void setPlaybackRate(double playbackRate);
    [[nodiscard]] double playbackRate() const;

signals:
    void mediaInfoReady(qint64 durationMs, int width, int height,
                        double fps, int sampleRate, int channels,
                        quint64 channelLayoutMask,
                        int sampleFmt,
                        const QString& format);
    void errorOccurred(const QString& message);
    void endOfFile();
    void positionChanged(qint64 posMs);
    void seekCompleted(int generation, int serial);
    void endOfAudio();
    void endOfVideo();
    void nativeRenderingFailed();
    void playbackRateChanged(double playbackRate);

private:
    class SessionEventBridge;

    void postSessionEvent(const media_sdk::PlayerEvent& event, std::uint64_t eventSerial);
    void postRuntimeDiagnostics(media_sdk::runtime::RuntimeDiagnostics diagnostics,
                                std::uint64_t eventSerial);
    void postNativeRenderingFailed(std::uint64_t eventSerial);
    void handleSessionEvent(media_sdk::PlayerEvent event, std::uint64_t eventSerial);
    void handleRuntimeDiagnostics(media_sdk::runtime::RuntimeDiagnostics diagnostics,
                                  std::uint64_t eventSerial);
    void handleNativeRenderingFailed(std::uint64_t eventSerial);
    media_sdk::session::PlaybackSessionConfig sessionConfig() const;
    bool acceptsEventSerial(std::uint64_t eventSerial) const;

    mutable std::mutex m_mutex;
    media_sdk::runtime::IAudioOutput* m_audioOutput = nullptr;
    media_sdk::runtime::IVideoPresenter* m_videoPresenter = nullptr;
    media_sdk::runtime::IAudioTempoProcessor* m_audioTempoProcessor = nullptr;
    SdkPlaybackSessionFactory m_sessionFactory;
    std::unique_ptr<ISdkPlaybackSession> m_session;
    std::unique_ptr<SessionEventBridge> m_sessionEventBridge;
    PendingSeekRequests<int> m_pendingSeekRequests;
    std::uint64_t m_eventSerial = 0;
    bool m_directNativeVideoEnabled = false;
    bool m_paused = false;
    float m_volume = 1.0f;
    bool m_muted = false;
    double m_confirmedPlaybackRate = media_sdk::runtime::kDefaultPlaybackRate;
    double m_configuredPlaybackRate = media_sdk::runtime::kDefaultPlaybackRate;
    bool m_rateChangePending = false;
};
