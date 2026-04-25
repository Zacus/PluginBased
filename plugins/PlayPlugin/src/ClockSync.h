#pragma once

#include <QAtomicInteger>
#include <QtGlobal>
#include <limits>

/**
 * @brief ClockSync — 音视频同步时钟
 *
 * 以音频时钟为主（工业标准：人耳对音频抖动更敏感）。
 *
 * AudioRenderer 持续更新 audioClock（已渲染的音频时间，单位：微秒）。
 * VideoRenderer 读取 audioClock，根据当前帧 PTS 决定：
 *   - PTS 比时钟早超过 EARLY_THRESHOLD_US  → 等待（帧还没到时间）
 *   - PTS 比时钟晚超过  LATE_THRESHOLD_US  → 丢帧（落后太多，追帧）
 *   - 否则                                 → 渲染
 *
 * 使用 QAtomicInteger<qint64> 保证无锁读写，不需要 mutex。
 */
class ClockSync
{
public:
    // 同步阈值（微秒）
    static constexpr qint64 EARLY_THRESHOLD_US =  40'000;  // 早于时钟 40ms → 等待
    static constexpr qint64 LATE_THRESHOLD_US  = 100'000;  // 晚于时钟 100ms → 丢帧

    static constexpr qint64 INVALID_CLOCK = std::numeric_limits<qint64>::min();

    ClockSync() { m_audioClock.storeRelaxed(INVALID_CLOCK); }

    // ── AudioRenderer 写 ─────────────────────────────────────────────────────
    void setAudioClock(qint64 us) { m_audioClock.storeRelaxed(us); }
    void invalidate()             { m_audioClock.storeRelaxed(INVALID_CLOCK); }

    // ── VideoRenderer 读 ─────────────────────────────────────────────────────
    qint64 audioClock() const { return m_audioClock.loadRelaxed(); }
    bool   isValid()    const { return audioClock() != INVALID_CLOCK; }

    /**
     * @brief 判断视频帧该如何处理
     * @param framePtsUs 帧的 PTS（微秒）
     */
    enum class Action { Render, Wait, Drop };
    Action decide(qint64 framePtsUs) const
    {
        if (!isValid()) return Action::Render; // 无音频时直接渲染

        const qint64 diff = framePtsUs - audioClock(); // 正数：帧还没到时间
        if (diff >  EARLY_THRESHOLD_US) return Action::Wait;
        if (diff < -LATE_THRESHOLD_US)  return Action::Drop;
        return Action::Render;
    }

private:
    QAtomicInteger<qint64> m_audioClock;
};
