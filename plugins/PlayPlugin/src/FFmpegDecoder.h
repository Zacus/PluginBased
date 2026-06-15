#pragma once

#include "FFmpegUtils.h"
#include "FrameQueue.h"
#include "decode/DecodePerformance.h"
#include "decode/VideoFrameProcessor.h"
#include "hw/HardwareDecoderBackend.h"

#include <QThread>
#include <QUrl>
#include <QString>
#include <QMutex>
#include <QAtomicInt>
#include <QWaitCondition>

#include <memory>

/**
 * @brief FFmpegDecoder — 独立解码线程
 *
 * 职责：
 *   - 打开媒体文件（avformat_open_input）
 *   - 探测流信息（avformat_find_stream_info）
 *   - 找到最佳视频流 / 音频流，打开对应解码器
 *   - 主循环：av_read_frame → avcodec_send_packet → avcodec_receive_frame
 *   - 解码好的 AVFrame 写入 VideoFrameQueue / AudioFrameQueue
 *   - 支持 seek：发送 flush packet，清空队列，跳转到目标位置
 *   - 支持暂停：在主循环里等待，不消耗 CPU
 *
 * 线程模型：
 *   FFmpegDecoder 运行在独立 QThread 上。
 *   PlayerEngine（主线程）通过信号槽发送 openFile / seekTo / setPaused / stop 指令，
 *   解码线程通过信号向主线程报告 mediaInfoReady / errorOccurred / endOfFile。
 *
 * 队列所有权：
 *   VideoFrameQueue 和 AudioFrameQueue 由 PlayerEngine 持有，
 *   构造时以指针传入，解码器只写，渲染器只读。
 */
class FFmpegDecoder : public QThread
{
    Q_OBJECT

public:
    explicit FFmpegDecoder(VideoFrameQueue* videoQ,
                           AudioFrameQueue* audioQ,
                           QObject* parent = nullptr);
    ~FFmpegDecoder() override;

    // ── 媒体信息（主线程只读，open 完成后有效）────────────────────────────
    qint64 durationMs()   const { return m_durationMs; }
    int    videoWidth()   const { return m_videoWidth; }
    int    videoHeight()  const { return m_videoHeight; }
    double videoFps()     const { return m_videoFps; }
    int    audioChannels()const { return m_audioChannels; }
    int    audioSampleRate() const { return m_audioSampleRate; }
    QString formatName()  const { return m_formatName; }

public slots:
    /** 打开文件并开始解码（线程安全，可从主线程调用）*/
    void openFile(const QUrl& url);

    /** seek 到指定位置（毫秒）*/
    void seekTo(qint64 posMs, int generation = 0);

    /** 暂停 / 恢复解码循环 */
    void setPaused(bool paused);

    /** 停止解码，退出线程 */
    void stopDecoding();

    /** 启用后保留 VideoToolbox 硬解帧给 Surface 原生渲染；关闭时走 CPU fallback */
    void setVideoToolboxDirectRenderingEnabled(bool enabled);

signals:
    /** 文件打开成功，媒体信息已就绪 */
    void mediaInfoReady(qint64 durationMs, int width, int height,
                        double fps, int sampleRate, int channels,
                        quint64 channelLayoutMask,
                        int sampleFmt,       // AVSampleFormat，用 int 传递避免跨模块枚举依赖
                        const QString& format);

    /** 解码出错 */
    void errorOccurred(const QString& message);

    /** 文件播放结束（所有帧已解码完毕）*/
    void endOfFile();

    /** 当前解码位置更新（毫秒），供主线程同步进度条 */
    void positionChanged(qint64 posMs);

    /** seek 已在解码线程完成，generation 对应 seekTo() 的调用方标识，serial 为新帧序列 */
    void seekCompleted(int generation, int serial);

protected:
    void run() override;

private:
    // ── 内部方法 ─────────────────────────────────────────────────────────────
    bool openInternal(const QString& path);
    bool openVideoCodec(AVStream* stream, const AVCodec* codec);
    AVCodecContext* createVideoCodecContext(AVStream* stream, const AVCodec* codec);
    void decodeLoop();
    void doSeek(qint64 posMs, int serial);
    bool sendPacketToDecoder(AVCodecContext* ctx, AVPacket* pkt,
                             FrameQueue<AVFramePtr>* queue, int serial);
    void closeInternal();

    // ── 队列（外部持有，不拥有所有权）───────────────────────────────────────
    VideoFrameQueue* m_videoQueue = nullptr;
    AudioFrameQueue* m_audioQueue = nullptr;

    // ── FFmpeg 上下文（线程内创建和销毁）─────────────────────────────────────
    AVFormatContextPtr m_fmtCtx;
    AVCodecContextPtr  m_videoCodecCtx;
    AVCodecContextPtr  m_audioCodecCtx;
    std::unique_ptr<HardwareDecoderBackend> m_hardwareDecoder;
    QString m_activeVideoDecoderName = QStringLiteral("software");
    DecodePerformanceLogger m_decodePerf;
    VideoFrameProcessor m_videoFrameProcessor;
    int  m_videoStreamIdx = -1;
    int  m_audioStreamIdx = -1;

    // ── 媒体信息（open 后写入，之后只读）─────────────────────────────────────
    qint64  m_durationMs      = 0;
    int     m_videoWidth      = 0;
    int     m_videoHeight     = 0;
    double  m_videoFps        = 0.0;
    int     m_audioChannels   = 0;
    int     m_audioSampleRate = 0;
    quint64 m_audioChannelLayoutMask = 0;
    int     m_audioSampleFmt  = AV_SAMPLE_FMT_FLTP; // AVSampleFormat，存 int 避免枚举依赖
    QString m_formatName;

    // ── 控制变量（跨线程，用原子或 mutex 保护）───────────────────────────────
    QAtomicInt  m_stop   { 0 };   // 1 = 请求停止
    QAtomicInt  m_paused { 0 };   // 1 = 暂停
    QAtomicInt  m_videoToolboxDirectRenderingEnabled { 0 };

    QMutex         m_seekMutex;
    QWaitCondition m_seekCond;
    bool   m_seekRequested = false;
    qint64 m_seekTargetMs  = 0;
    int    m_seekGeneration = 0;

    QMutex         m_openMutex;
    QWaitCondition m_openCond;
    bool    m_openRequested = false;
    QString m_pendingPath;

    // flush serial：每次 seek 递增，消费侧丢弃旧 serial 的帧
    int m_flushSerial = 0;
};
