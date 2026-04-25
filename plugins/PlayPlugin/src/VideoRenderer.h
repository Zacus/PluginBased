#pragma once

#include "FrameQueue.h"
#include "ClockSync.h"
#include "FFmpegUtils.h"

#include <QObject>
#include <QTimer>
#include <QImage>
#include <QSize>

/**
 * @brief VideoRenderer — 视频帧渲染器（主线程）
 *
 * 用 QTimer（默认 8ms，约 120fps 上限）驱动，运行在主线程。
 * 从 VideoFrameQueue 取帧，与音频时钟同步后，用 swscale 转换
 * YUV → RGB32，通过 frameReady 信号发给 FFmpegSurface 显示。
 *
 * 同步策略（以音频时钟为主）：
 *   - 帧 PTS 比音频时钟早 > 40ms → 不取帧，等下次 timer 触发
 *   - 帧 PTS 比音频时钟晚 > 100ms → 丢帧，取下一帧
 *   - 否则渲染
 *
 * 运行在主线程的理由：
 *   QSGNode 更新只能在 Qt 渲染线程或主线程（通过 update()）触发，
 *   用 QTimer 在主线程取帧是最简单可靠的方式，避免跨线程操作 QQuickItem。
 */
class VideoRenderer : public QObject
{
    Q_OBJECT

public:
    explicit VideoRenderer(VideoFrameQueue* queue,
                           ClockSync*       clock,
                           QObject*         parent = nullptr);
    ~VideoRenderer() override;

    void start();                // 开始渲染定时器
    void stop();                 // 停止渲染定时器，清理 swscale（用于完全停止）
    void flush();                // seek 时调用

    // 通知视频帧尺寸（解码器 open 后调用，用于初始化 swscale）
    void setSourceSize(int width, int height, AVPixelFormat fmt);

signals:
    /** 每一帧渲染好的 QImage，发给 FFmpegSurface */
    void frameReady(const QImage& image);

    /** 视频流 EOF */
    void endOfVideo();

private slots:
    void onTimer();

private:
    bool initSwsContext();
    QImage convertFrame(AVFrame* frame);

    VideoFrameQueue* m_queue = nullptr;
    ClockSync*       m_clock = nullptr;

    QTimer  m_timer;

    // swscale 上下文
    SwsContextPtr m_swsCtx;
    int           m_srcWidth  = 0;
    int           m_srcHeight = 0;
    AVPixelFormat m_srcFmt    = AV_PIX_FMT_YUV420P;

    // 输出缓冲（复用避免频繁分配）
    QImage m_outputImage;

    // 上次 Wait 时暂存的帧（下次 timer 优先消费）
    VideoFrameQueue::Entry m_heldEntry;
    bool                   m_hasHeld = false;

    // 当前帧的 flush serial（用于丢弃 seek 前的旧帧）
    int m_currentSerial = 0;
};
