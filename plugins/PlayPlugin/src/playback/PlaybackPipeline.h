#pragma once

// PlaybackPipeline 是 PlayPlugin 内部的播放管线协调器。
// C 阶段后 PlayPlugin 只持有 Qt presenter/audio output，并把播放编排交给 SDK PlaybackSession。

#include "playback/SdkPlaybackAdapter.h"

#include <QObject>
#include <QPointer>
#include <QUrl>
#include <memory>

class FFmpegSurface;
class QtRhiVideoPresenter;

namespace media_sdk::runtime {
class IAudioOutput;
}

namespace media_sdk::platform::macos {
class CoreAudioAudioOutput;
}

class PlaybackPipeline : public QObject
{
    Q_OBJECT

public:
    explicit PlaybackPipeline(QObject* parent = nullptr);
    ~PlaybackPipeline() override;

    void setSurface(FFmpegSurface* surface);
    void clearSurface();

    void openFile(const QUrl& url);
    void startRenderersForMedia(bool hasAudio,
                                bool hasVideo,
                                int sampleRate,
                                int channels,
                                quint64 channelLayoutMask,
                                int sampleFmt);
    void setPaused(bool paused);
    void setVolume(float volume);
    void setMuted(bool muted);
    void stopComponents();
    void seek(qint64 positionMs, int generation, bool resumeAfterSeek = false);

signals:
    void mediaInfoReady(qint64 durationMs, int width, int height,
                        double fps, int sampleRate, int channels,
                        quint64 channelLayoutMask,
                        int sampleFmt,
                        const QString& format);
    void errorOccurred(const QString& message);
    void endOfFile();
    void decoderPositionChanged(qint64 posMs);
    void audioPositionChanged(qint64 posMs);
    void endOfAudio();
    void videoPositionChanged(qint64 posMs);
    void endOfVideo();
    void seekCompleted(int generation, int serial);
    void nativeRenderingFailed();

private slots:
    void onDecoderSeekCompleted(int generation, int serial);
    void onSurfaceNativeRenderingFailed();
    void onSdkNativeRenderingFailed();

private:
    void createSdkRuntimeChain();
    void destroySdkRuntimeChain();
    void updateNativeVideoRenderingEnabled();
    void disableNativeVideoRenderingAfterFailure(bool notifySdkSession);

    std::unique_ptr<SdkPlaybackAdapter> m_sdkAdapter;
    std::unique_ptr<QtRhiVideoPresenter> m_sdkVideoPresenter;
#if defined(Q_OS_APPLE)
    std::unique_ptr<media_sdk::platform::macos::CoreAudioAudioOutput> m_sdkAudioOutput;
#endif

    QPointer<FFmpegSurface> m_surface;
    bool m_nativeVideoRenderingEnabled = false;
};
