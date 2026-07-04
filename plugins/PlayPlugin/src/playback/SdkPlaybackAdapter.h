#pragma once

#include "media_sdk/session/PlaybackSession.h"
#include "playback/PendingSeekRequests.h"

#include <QObject>
#include <QUrl>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>

class SdkPlaybackAdapter final : public QObject
{
    Q_OBJECT

public:
    SdkPlaybackAdapter(media_sdk::runtime::IAudioOutput* audioOutput,
                       media_sdk::runtime::IVideoPresenter* videoPresenter,
                       QObject* parent = nullptr);
    ~SdkPlaybackAdapter() override;

    void openFile(const QUrl& url);
    void setPaused(bool paused);
    void seek(qint64 positionMs, int generation);
    void stopDecoding();
    void setVideoToolboxDirectRenderingEnabled(bool enabled);

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
    std::unique_ptr<media_sdk::session::PlaybackSession> m_session;
    std::unique_ptr<SessionEventBridge> m_sessionEventBridge;
    PendingSeekRequests<int> m_pendingSeekRequests;
    std::uint64_t m_eventSerial = 0;
    bool m_directNativeVideoEnabled = false;
    bool m_paused = false;
};
