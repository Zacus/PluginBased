#pragma once

#include "media_sdk/session/PlaybackSession.h"
#include "playback/PendingSeekRequests.h"

#include <QObject>
#include <QUrl>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>

class SdkPlaybackAdapter final
    : public QObject
    , public media_sdk::session::ISessionEvents
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
    void onEvent(const media_sdk::PlayerEvent& event) override;
    void onNativeRenderingFailed() override;
    void handleSessionEvent(media_sdk::PlayerEvent event, std::uint64_t eventSerial);
    void handleNativeRenderingFailed(std::uint64_t eventSerial);
    media_sdk::session::PlaybackSessionConfig sessionConfig() const;
    std::uint64_t currentEventSerial() const;
    bool acceptsEventSerial(std::uint64_t eventSerial) const;

    mutable std::mutex m_mutex;
    media_sdk::runtime::IAudioOutput* m_audioOutput = nullptr;
    media_sdk::runtime::IVideoPresenter* m_videoPresenter = nullptr;
    std::unique_ptr<media_sdk::session::PlaybackSession> m_session;
    PendingSeekRequests<int> m_pendingSeekRequests;
    std::uint64_t m_eventSerial = 0;
    bool m_directNativeVideoEnabled = false;
    bool m_paused = false;
};
