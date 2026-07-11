#pragma once

#include "media_sdk/runtime/RuntimeTypes.h"

#include <chrono>

namespace media_sdk::runtime {

struct SeekAudioGapDecision {
    bool shouldFill = false;
    bool exceedsMaxGap = false;
    std::chrono::microseconds target { 0 };
    std::chrono::microseconds firstAudioPts { 0 };
    std::chrono::microseconds gap { 0 };
    Generation generation = 0;
};

class SeekClockAnchor {
public:
    void begin(
        Generation generation,
        std::chrono::microseconds target,
        std::chrono::microseconds maxGap = std::chrono::milliseconds(2000))
    {
        m_generation = generation;
        m_target = target;
        m_maxGap = maxGap;
        m_active = true;
    }

    void clear()
    {
        m_active = false;
        m_generation = 0;
        m_target = std::chrono::microseconds { 0 };
        m_maxGap = std::chrono::microseconds { 0 };
    }

    [[nodiscard]] SeekAudioGapDecision inspectFirstAudio(
        Generation generation,
        std::chrono::microseconds firstAudioPts)
    {
        if (!m_active || generation != m_generation)
            return {};

        const auto target = m_target;
        const auto maxGap = m_maxGap;
        clear();

        const auto gap = firstAudioPts - target;
        if (gap <= std::chrono::microseconds::zero()) {
            return {
                .target = target,
                .firstAudioPts = firstAudioPts,
                .gap = gap,
                .generation = generation,
            };
        }

        return {
            .shouldFill = gap <= maxGap,
            .exceedsMaxGap = gap > maxGap,
            .target = target,
            .firstAudioPts = firstAudioPts,
            .gap = gap,
            .generation = generation,
        };
    }

    [[nodiscard]] bool activeFor(Generation generation) const
    {
        return m_active && generation == m_generation;
    }

    [[nodiscard]] std::chrono::microseconds target() const
    {
        return m_target;
    }

private:
    bool m_active = false;
    Generation m_generation = 0;
    std::chrono::microseconds m_target { 0 };
    std::chrono::microseconds m_maxGap { 0 };
};

} // namespace media_sdk::runtime
