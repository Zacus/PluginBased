#include "NativeFallbackController.h"

namespace media_sdk::runtime {

void NativeFallbackController::reset(SessionId sessionId, Generation generation, VideoOutputPolicy policy)
{
    m_sessionId = sessionId;
    m_generation = generation;
    m_pendingFallbackGeneration = 0;
    m_policy = policy;
    m_state = RuntimePlaybackState::Playing;
}

FallbackTransition NativeFallbackController::beginFallback(FallbackRequest request)
{
    FallbackTransition transition;
    if (request.sessionId != m_sessionId
        || request.generation != m_generation
        || !isFallbackReason(request.reason)
        || m_state == RuntimePlaybackState::FallbackPending) {
        transition.newGeneration = m_generation;
        transition.newPolicy = m_policy;
        return transition;
    }

    m_policy = VideoOutputPolicy::CpuOnly;
    m_pendingFallbackGeneration = m_generation + 1;
    m_generation = m_pendingFallbackGeneration;
    m_state = RuntimePlaybackState::FallbackPending;

    transition.accepted = true;
    transition.newGeneration = m_pendingFallbackGeneration;
    transition.newPolicy = m_policy;
    transition.pauseAudio = true;
    transition.flushAudio = true;
    transition.abortQueues = true;
    transition.clearPresenter = true;
    transition.requestCpuDecode = true;
    return transition;
}

bool NativeFallbackController::completeSeek(SessionId sessionId, Generation generation)
{
    if (m_state != RuntimePlaybackState::FallbackPending
        || sessionId != m_sessionId
        || generation != m_pendingFallbackGeneration) {
        return false;
    }

    m_pendingFallbackGeneration = 0;
    m_state = RuntimePlaybackState::Playing;
    return true;
}

RuntimePlaybackState NativeFallbackController::state() const
{
    return m_state;
}

VideoOutputPolicy NativeFallbackController::policy() const
{
    return m_policy;
}

bool NativeFallbackController::isFallbackReason(PresentStatus status)
{
    return status == PresentStatus::UnsupportedNativeHandle
        || status == PresentStatus::DeviceLost
        || status == PresentStatus::Failed;
}

} // namespace media_sdk::runtime
