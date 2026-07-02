#pragma once

// QtPlaybackAdapter bridges media_sdk::Player events back into the existing
// PlayPlugin queue/rendering pipeline. Control events are marshalled to the
// Qt object thread; data events use PlaybackDataBridge off the GUI thread.

#include "common/FrameQueue.h"
#include "media_sdk/DecodeFrameSink.h"
#include "media_sdk/Player.h"
#include "playback/PlaybackDataBridge.h"
#include "playback/PendingSeekRequests.h"

#include <QObject>
#include <QUrl>

#include <memory>

class QtPlaybackAdapter final
    : public QObject
    , public media_sdk::IEventSink
    , public media_sdk::IDecodeFrameSink
{
    Q_OBJECT

public:
    QtPlaybackAdapter(VideoFrameQueue* videoQueue,
                      AudioFrameQueue* audioQueue,
                      QObject* parent = nullptr);
    ~QtPlaybackAdapter() override;

    void openFile(const QUrl& url);
    void setPaused(bool paused);
    void seekTo(qint64 positionMs, int generation);
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

private:
    void onEvent(const media_sdk::PlayerEvent& event) override;
    media_sdk::DecodeFramePushResult pushAudio(
        media_sdk::AudioFrame frame,
        media_sdk::DecodeFrameMetadata metadata) override;
    media_sdk::DecodeFramePushResult pushVideo(
        media_sdk::VideoFrame frame,
        media_sdk::DecodeFrameMetadata metadata) override;

    bool handleDataEvent(const media_sdk::PlayerEvent& event);
    void handleEvent(const media_sdk::PlayerEvent& event);
    void resetPlayer();
    AVFramePtr makeAudioFrame(const media_sdk::AudioFrame& frame) const;
    AVFramePtr makeVideoFrame(const media_sdk::VideoFrame& frame) const;
    AVPixelFormat mapPixelFormat(media_sdk::PixelFormat format) const;
    AVSampleFormat mapSampleFormat(media_sdk::AudioSampleFormat format) const;
    void applyColorMetadata(const media_sdk::VideoFrame& source, AVFrame* destination) const;

    VideoFrameQueue* m_videoQueue = nullptr;
    AudioFrameQueue* m_audioQueue = nullptr;
    PlaybackDataBridge m_dataBridge;
    media_sdk::PlayerConfig m_config;
    std::unique_ptr<media_sdk::Player> m_player;
    int m_currentSerial = 0;
    PendingSeekRequests<int> m_pendingSeekRequests;
    bool m_hasAudio = false;
    bool m_hasVideo = false;
    bool m_directNativeVideoEnabled = false;
};
