#pragma once

#include "media_sdk/runtime/RuntimeTypes.h"
#include "media_sdk/runtime/VideoPresenter.h"

#include <chrono>

namespace media_sdk::runtime {

enum class RuntimePlaybackState {
    Idle,
    Opening,
    Playing,
    Paused,
    Seeking,
    FallbackPending,
    Draining,
    Stopped,
    Error
};

struct FallbackRequest {
    SessionId sessionId = 0;
    Generation generation = 0;
    PresentStatus reason = PresentStatus::Failed;
    std::chrono::microseconds resumePosition { 0 };
};

struct FallbackTransition {
    bool accepted = false;
    Generation newGeneration = 0;
    VideoOutputPolicy newPolicy = VideoOutputPolicy::PreferNative;
    bool pauseAudio = false;
    bool flushAudio = false;
    bool abortQueues = false;
    bool clearPresenter = false;
    bool requestCpuDecode = false;
};

class NativeFallbackController
{
public:
    void reset(SessionId sessionId, Generation generation, VideoOutputPolicy policy);
    FallbackTransition beginFallback(FallbackRequest request);
    bool completeSeek(SessionId sessionId, Generation generation);
    RuntimePlaybackState state() const;
    VideoOutputPolicy policy() const;

private:
    static bool isFallbackReason(PresentStatus status);

    SessionId m_sessionId = 0;
    Generation m_generation = 0;
    Generation m_pendingFallbackGeneration = 0;
    VideoOutputPolicy m_policy = VideoOutputPolicy::PreferNative;
    RuntimePlaybackState m_state = RuntimePlaybackState::Idle;
};

} // namespace media_sdk::runtime
