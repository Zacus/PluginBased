/*
 * @Author: zs
 * @Date: 2026-04-25 07:19:02
 * @LastEditors: zs
 * @LastEditTime: 2026-05-05 22:46:29
 * @FilePath: /PluginBased/plugins/PlayPlugin/src/audio/AudioRenderer.h
 * @Description:
 *
 * Copyright (c) 2026 by zs, All Rights Reserved.
 */
#pragma once

#include "sync/ClockSync.h"
#include "common/FFmpegUtils.h"
#include "common/FrameQueue.h"

#include <QAtomicInt>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMutex>
#include <QThread>

/**
 * @brief AudioRenderer — 音频渲染线程
 *
 * 从 AudioFrameQueue 取 AVFrame，经 libswresample 重采样后
 * 写入 QAudioSink，同时维护音频时钟供视频同步使用。
 *
 * 输出格式固定为：
 *   采样率：48000 Hz（或跟随源，见 TARGET_SAMPLE_RATE）
 *   声道数：2（立体声）
 *   格式  ：Float（QAudioFormat::Float，32-bit float）
 *
 * 线程安全说明：
 *   m_sink 在 run()（音频线程）里创建和销毁。
 *   主线程只能修改原子变量（m_paused/m_stop/m_flush）和受 m_paramMutex 保护的参数，
 *   绝不能直接调用 m_sink 的任何方法，避免 QAudioSink 跨线程竞争。
 *
 * 音频时钟计算：
 *   clock_us = m_sink->processedUSecs()（已实际送出到硬件的微秒数）
 */
class AudioRenderer : public QThread
{
    Q_OBJECT

  public:
    static constexpr int TARGET_SAMPLE_RATE = 48000;
    static constexpr int TARGET_CHANNELS = 2;

    explicit AudioRenderer(AudioFrameQueue* queue, ClockSync* clock, QObject* parent = nullptr);
    ~AudioRenderer() override;

    // ── 主线程控制接口 ────────────────────────────────────────────────────────
    void setVolume(float v); // 0.0 ~ 1.0
    void setMuted(bool muted);
    void setPaused(bool paused);
    void setAcceptedSerial(int serial);
    void stopRenderer();
    void flush(); // seek 时清空，配合 FrameQueue::flush()

    // 通知渲染器音频流参数（解码器 open 后调用）
    void setSourceFormat(int sampleRate, int channels, AVSampleFormat fmt,
                         quint64 channelLayoutMask = 0);

  signals:
    void positionChanged(qint64 posMs); // 当前音频播放位置（毫秒）
    void errorOccurred(const QString& msg);
    void endOfAudio();

  protected:
    void run() override;

  private:
    bool initSwrContext();
    void renderFrame(AVFrame* frame);
    bool handleFlush();
    void resetClockState();
    void updateClock(qint64 framePtsUs);

    AudioFrameQueue* m_queue = nullptr;
    ClockSync* m_clock = nullptr;

    // QAudioSink（在线程内创建，必须在同一线程销毁）
    QAudioSink* m_sink = nullptr;
    QIODevice* m_device = nullptr; // sink->start() 返回的写入设备

    // 重采样上下文
    SwrContextPtr m_swrCtx;

    // 源格式（由 setSourceFormat 设置）
    int m_srcSampleRate = 44100;
    int m_srcChannels = 2;
    quint64 m_srcChannelLayoutMask = 0;
    AVSampleFormat m_srcFmt = AV_SAMPLE_FMT_FLTP;

    // 音频时钟（上次发信号的时间，避免过于频繁 emit）
    qint64 m_lastEmitUs = 0;
    qint64 m_startPts = AV_NOPTS_VALUE;
    qint64 m_startSinkProcessedUs = 0;

    // 控制
    QAtomicInt m_stop{0};
    QAtomicInt m_paused{0};
    QAtomicInt m_flush{0};
    QAtomicInt m_acceptedSerial{0};

    float m_volume = 1.0f;
    bool m_muted = false;
    bool m_paramDirty = false; // 主线程修改了 volume/muted，等待音频线程应用
    QMutex m_paramMutex;       // 保护 volume / muted / m_paramDirty 的读写
};
