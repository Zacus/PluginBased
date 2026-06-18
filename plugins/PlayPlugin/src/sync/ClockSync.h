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
#include <limits>

class ClockSync
{
public:
    static constexpr qint64 EARLY_THRESHOLD_US =  40'000;
    static constexpr qint64 LATE_THRESHOLD_US  = 100'000;
    static constexpr qint64 INVALID_CLOCK = std::numeric_limits<qint64>::min();

    ClockSync() { m_audioClock.storeRelaxed(INVALID_CLOCK); }

    // AudioRenderer 写：每次 processedUSecs 有新值时调用
    void setAudioClock(qint64 us)
    {
        QMutexLocker lk(&m_mutex);
        m_baseAudioUs = us;
        m_elapsed.restart(); // 从这一刻开始用壁钟补偿
        m_paused = false;
        m_audioClock.storeRelaxed(us);
    }

    void invalidate()
    {
        QMutexLocker lk(&m_mutex);
        m_baseAudioUs = INVALID_CLOCK;
        m_paused = false;
        m_audioClock.storeRelaxed(INVALID_CLOCK);
    }

    void setPaused(bool paused)
    {
        QMutexLocker lk(&m_mutex);
        if (m_baseAudioUs == INVALID_CLOCK || m_paused == paused)
            return;

        if (paused)
        {
            m_baseAudioUs += m_elapsed.nsecsElapsed() / 1000;
            m_audioClock.storeRelaxed(m_baseAudioUs);
        }
        else
        {
            m_elapsed.restart();
        }

        m_paused = paused;
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

    bool isValid() const { return audioClock() != INVALID_CLOCK; }

    enum class Action { Render, Wait, Drop };
    Action decide(qint64 framePtsUs) const
    {
        const qint64 clk = audioClock();
        if (clk == INVALID_CLOCK) return Action::Render;

        const qint64 diff = framePtsUs - clk;
        if (diff >  EARLY_THRESHOLD_US) return Action::Wait;
        if (diff < -LATE_THRESHOLD_US)  return Action::Drop;
        return Action::Render;
    }

private:
    mutable QMutex   m_mutex;
    mutable QElapsedTimer m_elapsed;
    qint64           m_baseAudioUs = INVALID_CLOCK;
    bool             m_paused = false;
    QAtomicInteger<qint64> m_audioClock; // 保留原子，兼容旧读取
};
