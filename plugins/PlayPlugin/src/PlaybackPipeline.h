#pragma once

// PlaybackPipeline 是 PlayPlugin 内部的播放管线协调器。
// 它集中持有解码器、音频/视频渲染器、帧队列和时钟，让 PlayerEngine 只保留 QML-facing 状态。

#include "AudioRenderer.h"
#include "ClockSync.h"
#include "FFmpegDecoder.h"
#include "FrameQueue.h"
#include "VideoRenderer.h"

#include <QObject>
#include <QPointer>
#include <QUrl>
#include <memory>

class FFmpegSurface;

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
    void seek(qint64 positionMs, int generation);

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
    void onNativeRenderingFailed();

private:
    void updateNativeVideoRenderingEnabled();

    VideoFrameQueue m_videoQueue { 30 };
    AudioFrameQueue m_audioQueue { 64 };
    ClockSync m_clock;

    std::unique_ptr<FFmpegDecoder> m_decoder;
    std::unique_ptr<AudioRenderer> m_audioRenderer;
    std::unique_ptr<VideoRenderer> m_videoRenderer;

    QPointer<FFmpegSurface> m_surface;
    bool m_nativeVideoRenderingEnabled = false;
};
