/*
 * @Author: zs
 * @Date: 2026-04-07 15:39:45
 * @LastEditors: zs
 * @LastEditTime: 2026-04-27 00:02:15
 * @FilePath: /PluginBased/plugins/PlayPlugin/src/sync/ClockSync.h
 * @Description: 
 * 
 * Copyright (c) 2026 by zs, All Rights Reserved. 
 */
#pragma once

#include <QAtomicInteger>
#include <QElapsedTimer>
#include <QMutex>
#include <QtGlobal>
#include <atomic>
#include <chrono>
#include <limits>

class ClockSync
{
public:
    static constexpr qint64 EARLY_THRESHOLD_US =  40'000;
    static constexpr qint64 LATE_THRESHOLD_US  = 100'000;
    static constexpr qint64 INVALID_CLOCK = std::numeric_limits<qint64>::min();

    ClockSync() { publishAudioClockSnapshot(INVALID_CLOCK, false); }

    // AudioRenderer 写：每次 processedUSecs 有新值时调用
    void setAudioClock(qint64 us)
    {
        QMutexLocker lk(&m_mutex);
        m_baseAudioUs = us;
        m_elapsed.restart(); // 从这一刻开始用壁钟补偿
        m_paused = false;
        publishAudioClockSnapshot(us, false);
    }

    void invalidate()
    {
        QMutexLocker lk(&m_mutex);
        m_baseAudioUs = INVALID_CLOCK;
        m_paused = false;
        publishAudioClockSnapshot(INVALID_CLOCK, false);
    }

    void setPaused(bool paused)
    {
        QMutexLocker lk(&m_mutex);
        if (m_baseAudioUs == INVALID_CLOCK || m_paused == paused)
            return;

        if (paused)
        {
            m_baseAudioUs += m_elapsed.nsecsElapsed() / 1000;
        }
        else
        {
            m_elapsed.restart();
        }

        m_paused = paused;
        publishAudioClockSnapshot(m_baseAudioUs, m_paused);
    }

    // VideoRenderer 读：返回平滑后的时钟
    qint64 audioClock() const
    {
        QMutexLocker lk(&m_mutex);
        if (m_baseAudioUs == INVALID_CLOCK)
            return INVALID_CLOCK;
        if (m_paused)
            return m_baseAudioUs;
        // 用壁钟补偿 processedUSecs 的粗粒度更新
        return m_baseAudioUs + m_elapsed.nsecsElapsed() / 1000;
    }

    // VideoRenderer 高频读取：无锁读取最近一次音频锚点，并用单调壁钟补偿。
    qint64 audioClockFast() const
    {
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const quint64 before =
                m_audioClockSnapshotSequence.load(std::memory_order_acquire);
            if ((before & 1U) != 0)
                continue;

            const qint64 baseUs =
                m_audioClockSnapshotBaseUs.load(std::memory_order_relaxed);
            const qint64 anchorNs =
                m_audioClockSnapshotAnchorNs.load(std::memory_order_relaxed);
            const bool paused =
                m_audioClockSnapshotPaused.load(std::memory_order_relaxed);

            const quint64 after =
                m_audioClockSnapshotSequence.load(std::memory_order_acquire);
            if (before != after || (after & 1U) != 0)
                continue;

            if (baseUs == INVALID_CLOCK)
                return INVALID_CLOCK;
            if (paused)
                return baseUs;

            const qint64 elapsedUs = qMax<qint64>(0, monotonicNowNs() - anchorNs) / 1000;
            return baseUs + elapsedUs;
        }

        return m_audioClock.loadRelaxed();
    }

    bool isValid() const { return audioClock() != INVALID_CLOCK; }

    enum class Action { Render, Wait, Drop };
    Action decide(qint64 framePtsUs) const
    {
        const qint64 clk = audioClockFast();
        if (clk == INVALID_CLOCK) return Action::Render;

        const qint64 diff = framePtsUs - clk;
        if (diff >  EARLY_THRESHOLD_US) return Action::Wait;
        if (diff < -LATE_THRESHOLD_US)  return Action::Drop;
        return Action::Render;
    }

private:
    static qint64 monotonicNowNs()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void publishAudioClockSnapshot(qint64 baseAudioUs, bool paused)
    {
        m_audioClockSnapshotSequence.fetch_add(1, std::memory_order_acq_rel);
        m_audioClockSnapshotBaseUs.store(baseAudioUs, std::memory_order_relaxed);
        m_audioClockSnapshotAnchorNs.store(monotonicNowNs(), std::memory_order_relaxed);
        m_audioClockSnapshotPaused.store(paused, std::memory_order_relaxed);
        m_audioClock.storeRelaxed(baseAudioUs);
        m_audioClockSnapshotSequence.fetch_add(1, std::memory_order_release);
    }

    mutable QMutex   m_mutex;
    mutable QElapsedTimer m_elapsed;
    qint64           m_baseAudioUs = INVALID_CLOCK;
    bool             m_paused = false;
    QAtomicInteger<qint64> m_audioClock; // fast path 退避使用的最近音频时钟值
    mutable std::atomic<quint64> m_audioClockSnapshotSequence { 0 };
    mutable std::atomic<qint64> m_audioClockSnapshotBaseUs { INVALID_CLOCK };
    mutable std::atomic<qint64> m_audioClockSnapshotAnchorNs { 0 };
    mutable std::atomic<bool> m_audioClockSnapshotPaused { false };
};
