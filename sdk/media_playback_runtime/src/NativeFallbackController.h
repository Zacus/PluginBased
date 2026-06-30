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
    [[nodiscard("fallback transition tells runtime which queues, audio, and presenter state to reset")]]
    FallbackTransition beginFallback(FallbackRequest request);
    [[nodiscard("seek completion result determines whether fallback can resume")]]
    bool completeSeek(SessionId sessionId, Generation generation);
    [[nodiscard]]
    RuntimePlaybackState state() const;
    [[nodiscard]]
    VideoOutputPolicy policy() const;

private:
    [[nodiscard]]
    static bool isFallbackReason(PresentStatus status);

    SessionId m_sessionId = 0;
    Generation m_generation = 0;
    Generation m_pendingFallbackGeneration = 0;
    VideoOutputPolicy m_policy = VideoOutputPolicy::PreferNative;
    RuntimePlaybackState m_state = RuntimePlaybackState::Idle;
};

} // namespace media_sdk::runtime
