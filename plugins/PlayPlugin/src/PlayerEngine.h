#pragma once

#include <QObject>
#include <QUrl>
#include <QString>
#include <QPointer>
#include <QQmlEngine>
#include <memory>

#include "MediaInfo.h"
#include "FrameQueue.h"
#include "ClockSync.h"
#include "FFmpegDecoder.h"
#include "AudioRenderer.h"
#include "VideoRenderer.h"
#include "FFmpegSurface.h"

/**
 * @brief PlayerEngine — PlayPlugin 的核心播放引擎
 *
 * 持有并协调以下组件：
 *   FFmpegDecoder   解码线程，产出视频帧和音频帧
 *   AudioRenderer   音频渲染线程，消费音频帧，维护音频时钟
 *   VideoRenderer   视频渲染定时器（主线程），消费视频帧，同步时钟
 *   FFmpegSurface   QML 视频输出组件（由 QML 侧创建，Engine 只持有弱引用）
 *
 * QML 接口与之前完全保持不变：
 *   - 所有 Q_PROPERTY 和信号不变，QML 代码无需修改
 *   - position() 改为由 AudioRenderer::positionChanged 驱动，不再轮询
 */
class PlayerEngine : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int        playbackState READ playbackStateInt NOTIFY playbackStateChanged)
    Q_PROPERTY(qint64     position      READ position         NOTIFY positionChanged)
    Q_PROPERTY(qint64     duration      READ duration         NOTIFY durationChanged)
    Q_PROPERTY(float      volume        READ volume  WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool       muted         READ muted   WRITE setMuted  NOTIFY mutedChanged)
    Q_PROPERTY(MediaInfo* currentMedia  READ currentMedia     NOTIFY currentMediaChanged)
    Q_PROPERTY(QString    errorString   READ errorString      NOTIFY errorOccurred)

public:
    enum PlaybackState { Stopped = 0, Playing = 1, Paused = 2 };
    Q_ENUM(PlaybackState)

    explicit PlayerEngine(QObject* parent = nullptr);
    ~PlayerEngine() override;

    PlaybackState playbackState()    const { return m_state; }
    int           playbackStateInt() const { return static_cast<int>(m_state); }
    qint64        position()         const { return m_position; }
    qint64        duration()         const { return m_duration; }
    float         volume()           const { return m_volume; }
    bool          muted()            const { return m_muted; }
    MediaInfo*    currentMedia()     const { return m_mediaInfo; }
    QString       errorString()      const { return m_errorString; }

    void setVolume(float v);
    void setMuted(bool m);

    /** QML 侧把 FFmpegSurface 对象传入，Engine 连接 VideoRenderer::frameReady */
    Q_INVOKABLE void setSurface(FFmpegSurface* surface);

public slots:
    Q_INVOKABLE void open(const QUrl& url);
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 positionMs);
    Q_INVOKABLE void togglePlayPause();

signals:
    void playbackStateChanged(int state);
    void positionChanged(qint64 posMs);
    void durationChanged(qint64 durMs);
    void volumeChanged(float volume);
    void mutedChanged(bool muted);
    void currentMediaChanged(MediaInfo* info);
    void errorOccurred(const QString& msg);
    void endOfMedia();

private slots:
    // 来自 FFmpegDecoder 的信号
    void onMediaInfoReady(qint64 durationMs, int width, int height,
                          double fps, int sampleRate, int channels,
                          int sampleFmt, const QString& format);
    void onDecoderError(const QString& msg);
    void onEndOfFile();
    void onDecoderPosition(qint64 posMs);
    void onDecoderSeekCompleted(int generation, int serial);

    // 来自 AudioRenderer 的信号
    void onAudioPosition(qint64 posMs);
    void onEndOfAudio();

    // 来自 VideoRenderer 的信号
    void onVideoPosition(qint64 posMs);
    void onEndOfVideo();

private:
    void setState(PlaybackState s);
    void setError(const QString& msg);
    void stopAllComponents();
    void maybeFinishMedia();
    void finishMedia();

    // ── 帧队列（PlayerEngine 持有，Decoder 写，Renderer 读）─────────────────
    VideoFrameQueue m_videoQueue { 30 }; // 视频队列：30帧约1秒缓冲
    AudioFrameQueue m_audioQueue { 64 }; // 音频队列：64帧

    // ── 时钟（AudioRenderer 写，VideoRenderer 读）────────────────────────────
    ClockSync m_clock;

    // ── 组件（unique_ptr，生命周期由 PlayerEngine 管理）─────────────────────
    std::unique_ptr<FFmpegDecoder>  m_decoder;
    std::unique_ptr<AudioRenderer>  m_audioRenderer;
    std::unique_ptr<VideoRenderer>  m_videoRenderer;

    // ── QML 侧的渲染组件（弱引用，不拥有所有权）─────────────────────────────
    QPointer<FFmpegSurface> m_surface;

    // ── 播放状态 ─────────────────────────────────────────────────────────────
    MediaInfo*    m_mediaInfo  = nullptr;
    PlaybackState m_state      = Stopped;
    qint64        m_position   = 0;
    qint64        m_duration   = 0;
    float         m_volume     = 1.0f;
    bool          m_muted      = false;
    bool          m_hasAudio   = false;
    bool          m_hasVideo   = false;
    bool          m_decoderFinished = false;
    bool          m_audioFinished = false;
    bool          m_videoFinished = false;
    bool          m_mediaFinished = false;
    int           m_seekGeneration = 0;
    QString       m_errorString;
    QUrl          m_currentUrl; // open() 时记录，onMediaInfoReady 时写入 MediaInfo
};
