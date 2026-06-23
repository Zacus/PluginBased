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

    PlaybackDataBridge(VideoFrameQueue* videoQueue, AudioFrameQueue* audioQueue);

    void reset(StreamState state);
    void setGeneration(std::uint64_t sessionId, std::uint64_t generation);
    void cancel();
    void cancelGeneration();

    bool pushAudio(AVFramePtr frame, std::uint64_t sessionId, std::uint64_t generation);
    bool pushVideo(AVFramePtr frame, std::uint64_t sessionId, std::uint64_t generation);
    bool finish(std::uint64_t sessionId, std::uint64_t generation);

private:
    StreamState snapshot() const;
    bool accepts(const StreamState& state,
                 std::uint64_t sessionId,
                 std::uint64_t generation) const;

    VideoFrameQueue* m_videoQueue = nullptr;
    AudioFrameQueue* m_audioQueue = nullptr;
    mutable std::mutex m_mutex;
    StreamState m_state;
};
