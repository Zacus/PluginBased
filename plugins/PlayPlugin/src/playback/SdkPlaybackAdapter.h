#pragma once

#include "media_sdk/DecodeFrameSink.h"
#include "media_sdk/Player.h"
#include "media_sdk/runtime/RuntimePlayer.h"
#include "playback/PendingSeekRequests.h"

#include <QObject>
#include <QUrl>

#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>

class SdkPlaybackAdapter final
    : public QObject
    , public media_sdk::IEventSink
    , public media_sdk::IDecodeFrameSink
    , public media_sdk::runtime::IRuntimePlayerEvents
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
    struct PendingSeekRequest {
        int qtGeneration = 0;
        media_sdk::runtime::RuntimeTimeline runtimeTimeline {};
    };

    struct AcceptedSeekCompletion {
        int qtGeneration = 0;
        bool hasQtGeneration = false;
        media_sdk::runtime::RuntimeTimeline runtimeTimeline {};
    };

    void onEvent(const media_sdk::PlayerEvent& event) override;
    media_sdk::DecodeFramePushResult pushAudio(
        media_sdk::AudioFrame frame,
        media_sdk::DecodeFrameMetadata metadata) override;
    media_sdk::DecodeFramePushResult pushVideo(
        media_sdk::VideoFrame frame,
        media_sdk::DecodeFrameMetadata metadata) override;
    void onFallbackToCpuRequested(media_sdk::runtime::RuntimeFallbackAction action) override;
    void onEndOfStreamPresented(media_sdk::runtime::RuntimeTimeline timeline) override;

    bool handleDataEvent(const media_sdk::PlayerEvent& event);
    void handleControlEvent(
        const media_sdk::PlayerEvent& event,
        std::optional<AcceptedSeekCompletion> acceptedSeekCompletion);
    std::optional<AcceptedSeekCompletion> acceptSeekCompletedEvent(const media_sdk::PlayerEvent& event);
    void handleFallbackOnObjectThread(media_sdk::runtime::RuntimeFallbackAction action);
    void resetPlayer(bool preferNativeVideoFrames);
    bool openCorePlayer(const std::filesystem::path& path);
    bool ensureRuntimeForMedia(const media_sdk::MediaInfo& info, bool preferNativeVideoFrames);
    media_sdk::runtime::RuntimeTimeline currentTimeline() const;
    media_sdk::runtime::RuntimeAudioFrame runtimeAudioFrame(
        const media_sdk::AudioFrame& frame,
        media_sdk::runtime::RuntimeTimeline timeline) const;
    media_sdk::runtime::RuntimeVideoFrame runtimeVideoFrame(
        media_sdk::VideoFrame frame,
        media_sdk::runtime::RuntimeTimeline timeline) const;
    bool acceptsCoreEvent(const media_sdk::EventMetadata& metadata) const;
    std::optional<media_sdk::runtime::RuntimeTimeline> acceptedRuntimeTimelineForCoreEvent(
        const media_sdk::EventMetadata& metadata) const;
    bool acceptsRuntimeTimeline(media_sdk::runtime::RuntimeTimeline timeline) const;
    void setAcceptedCoreTimeline(const media_sdk::EventMetadata& metadata,
                                 media_sdk::runtime::RuntimeTimeline runtimeTimeline);

    mutable std::mutex m_mutex;
    media_sdk::runtime::IAudioOutput* m_audioOutput = nullptr;
    media_sdk::runtime::IVideoPresenter* m_videoPresenter = nullptr;
    media_sdk::PlayerConfig m_config;
    std::unique_ptr<media_sdk::Player> m_player;
    std::shared_ptr<media_sdk::runtime::RuntimePlayer> m_runtimePlayer;
    std::filesystem::path m_currentPath;
    media_sdk::EventMetadata m_acceptedCoreTimeline {};
    media_sdk::runtime::RuntimeTimeline m_runtimeTimeline {};
    std::optional<media_sdk::runtime::RuntimeFallbackAction> m_pendingFallback;
    PendingSeekRequests<PendingSeekRequest> m_pendingSeekRequests;
    bool m_acceptingRuntimeFrames = false;
    bool m_directNativeVideoEnabled = false;
    bool m_paused = false;
};
