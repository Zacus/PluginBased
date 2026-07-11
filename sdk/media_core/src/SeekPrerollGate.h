#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace media_sdk {

enum class SeekPrerollAction {
    Accept,
    Discard,
    Stale
};

struct SeekPrerollDecision {
    SeekPrerollAction action = SeekPrerollAction::Discard;
};

struct SeekPrerollGateConfig {
    std::chrono::microseconds target { 0 };
    std::uint64_t generation = 0;
    bool hasVideo = false;
    bool hasAudio = false;
    bool preferAudioCompletion = false;
    bool allowVideoTailFallback = false;
    int maxDiscardedVideoFrames = 300;
    int maxDiscardedAudioFrames = 1000;
};

class SeekPrerollGate
{
public:
    explicit SeekPrerollGate(SeekPrerollGateConfig config)
        : m_config(config)
    {
    }

    [[nodiscard("caller must decide whether to drop, accept, or ignore stale video")]]
    SeekPrerollDecision inspectVideo(std::chrono::microseconds pts,
                                     std::uint64_t generation) const
    {
        if (generation != m_config.generation)
            return { .action = SeekPrerollAction::Stale };

        return { .action = actionForPts(pts) };
    }

    [[nodiscard("missing video PTS must be bounded by discard fallback, not accepted as exact")]]
    SeekPrerollDecision inspectMissingVideoPts(std::uint64_t generation) const
    {
        if (generation != m_config.generation)
            return { .action = SeekPrerollAction::Stale };

        return { .action = SeekPrerollAction::Discard };
    }

    [[nodiscard("caller must decide whether to drop, accept, or ignore stale audio")]]
    SeekPrerollDecision inspectAudio(std::chrono::microseconds pts,
                                     std::uint64_t generation) const
    {
        if (generation != m_config.generation)
            return { .action = SeekPrerollAction::Stale };

        return { .action = actionForPts(pts) };
    }

    void markVideoAccepted()
    {
        markVideoAccepted(m_config.target);
    }

    void markVideoAccepted(std::chrono::microseconds pts)
    {
        m_videoReady = true;
        if (!m_firstVideoPts.has_value())
            m_firstVideoPts = pts;
    }

    void markAudioAccepted()
    {
        markAudioAccepted(m_config.target);
    }

    void markAudioAccepted(std::chrono::microseconds pts)
    {
        m_audioReady = true;
        if (!m_firstAudioPts.has_value())
            m_firstAudioPts = pts;
    }

    void markVideoDiscarded()
    {
        ++m_discardedVideoFrames;
    }

    void markAudioDiscarded()
    {
        ++m_discardedAudioFrames;
    }

    [[nodiscard]] bool discardLimitReached() const
    {
        // 兜底边界：异常时间戳或稀疏媒体不能让精确 seek 无限预滚。
        return (m_config.hasVideo
                && m_config.maxDiscardedVideoFrames >= 0
                && m_discardedVideoFrames >= m_config.maxDiscardedVideoFrames)
            || (m_config.hasAudio
                && m_config.maxDiscardedAudioFrames >= 0
                && m_discardedAudioFrames >= m_config.maxDiscardedAudioFrames);
    }

    [[nodiscard]] bool shouldEmitCompletion() const
    {
        if (m_completionSent)
            return false;

        // 播放态允许首个目标侧音频或视频建立新 generation，避免另一条流先到时被上层判旧；
        // 后续仍等双流都到达目标侧再 retire，继续过滤目标前帧。
        if (m_config.preferAudioCompletion && m_config.hasAudio)
            return m_audioReady || (m_config.hasVideo && m_videoReady);

        // 暂停态仍以视频预滚完成为准，避免音频 backpressure 阻塞首帧显示。
        if (m_config.hasVideo)
            return m_videoReady;
        if (m_config.hasAudio)
            return m_audioReady;
        return false;
    }

    [[nodiscard]] bool readyToRetire() const
    {
        // audio+video seek 的完成可由 video 触发，但 audio 仍要继续裁剪到首个目标侧帧。
        return (!m_config.hasVideo || m_videoReady)
            && (!m_config.hasAudio || m_audioReady);
    }

    void markCompletionSent()
    {
        m_completionSent = true;
    }

    [[nodiscard]] bool completionSent() const
    {
        return m_completionSent;
    }

    [[nodiscard]] bool allowsVideoTailFallback() const
    {
        return m_config.allowVideoTailFallback;
    }

    [[nodiscard]] std::chrono::microseconds completionPosition() const
    {
        return m_config.target;
    }

    [[nodiscard]] std::optional<std::chrono::microseconds> firstVideoPts() const
    {
        return m_firstVideoPts;
    }

    [[nodiscard]] std::optional<std::chrono::microseconds> firstAudioPts() const
    {
        return m_firstAudioPts;
    }

private:
    [[nodiscard]] SeekPrerollAction actionForPts(std::chrono::microseconds pts) const
    {
        // 回退到关键帧后的预滚阶段，目标点前的帧不能进入 runtime 队列。
        return pts < m_config.target ? SeekPrerollAction::Discard : SeekPrerollAction::Accept;
    }

    SeekPrerollGateConfig m_config;
    std::optional<std::chrono::microseconds> m_firstVideoPts;
    std::optional<std::chrono::microseconds> m_firstAudioPts;
    int m_discardedVideoFrames = 0;
    int m_discardedAudioFrames = 0;
    bool m_videoReady = false;
    bool m_audioReady = false;
    bool m_completionSent = false;
};

} // namespace media_sdk
