#pragma once

#include "common/FFmpegUtils.h"
#include "common/FrameQueue.h"

#include <cstdint>
#include <mutex>

class PlaybackDataBridge final
{
public:
    struct StreamState {
        bool hasAudio = false;
        bool hasVideo = false;
        std::uint64_t sessionId = 0;
        std::uint64_t generation = 0;
    };

    struct PlaybackDataBridgeStats {
        std::uint64_t audioAccepted = 0;
        std::uint64_t videoAccepted = 0;
        std::uint64_t audioRejectedStale = 0;
        std::uint64_t videoRejectedStale = 0;
        std::uint64_t eofAccepted = 0;
        std::uint64_t queueAbortFailures = 0;
    };

    enum class PushStatus {
        Accepted,
        StaleGeneration,
        Cancelled,
        Closed
    };

    struct PushResult {
        PushStatus status = PushStatus::Closed;
    };

    PlaybackDataBridge(VideoFrameQueue* videoQueue, AudioFrameQueue* audioQueue);

    void reset(StreamState state);
    void setGeneration(std::uint64_t sessionId, std::uint64_t generation);
    void cancel();
    void cancelGeneration();

    [[nodiscard("push result distinguishes accepted, stale generation, cancellation, and closed bridge")]]
    PushResult pushAudio(AVFramePtr frame, std::uint64_t sessionId, std::uint64_t generation);
    [[nodiscard("push result distinguishes accepted, stale generation, cancellation, and closed bridge")]]
    PushResult pushVideo(AVFramePtr frame, std::uint64_t sessionId, std::uint64_t generation);
    bool finish(std::uint64_t sessionId, std::uint64_t generation);

private:
    enum class Counter {
        AudioAccepted,
        VideoAccepted,
        AudioRejectedStale,
        VideoRejectedStale,
        EofAccepted,
        QueueAbortFailure,
    };

    struct StatsSnapshot {
        StreamState state;
        PlaybackDataBridgeStats stats;
    };

    StreamState snapshot() const;
    bool accepts(const StreamState& state,
                 std::uint64_t sessionId,
                 std::uint64_t generation) const;
    bool acceptsLocked(std::uint64_t sessionId, std::uint64_t generation) const;
    void incrementCounter(std::uint64_t sessionId, std::uint64_t generation, Counter counter);
    void incrementCounterLocked(std::uint64_t sessionId,
                                std::uint64_t generation,
                                Counter counter);
    bool takeStatsSnapshotLocked(StatsSnapshot& snapshot);
    bool takeStatsSnapshot(StatsSnapshot& snapshot);
    void resetStatsLocked(StreamState state);
    void logStatsSummary(const char* reason, const StatsSnapshot& snapshot) const;

    VideoFrameQueue* m_videoQueue = nullptr;
    AudioFrameQueue* m_audioQueue = nullptr;
    mutable std::mutex m_mutex;
    StreamState m_state;
    StreamState m_statsState;
    PlaybackDataBridgeStats m_stats;
    bool m_statsLogged = true;
};
