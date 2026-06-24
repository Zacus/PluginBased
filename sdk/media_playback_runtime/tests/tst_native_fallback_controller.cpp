#include "NativeFallbackController.h"

#include <cassert>
#include <chrono>

using namespace std::chrono_literals;

namespace {

media_sdk::runtime::FallbackRequest request(
    media_sdk::runtime::SessionId sessionId,
    media_sdk::runtime::Generation generation,
    media_sdk::runtime::PresentStatus reason = media_sdk::runtime::PresentStatus::DeviceLost)
{
    return media_sdk::runtime::FallbackRequest {
        .sessionId = sessionId,
        .generation = generation,
        .reason = reason,
        .resumePosition = 1234ms,
    };
}

void currentGenerationNativeFailureStartsFallbackPending()
{
    media_sdk::runtime::NativeFallbackController controller;
    controller.reset(10, 4, media_sdk::runtime::VideoOutputPolicy::PreferNative);

    const auto transition = controller.beginFallback(request(10, 4));
    assert(transition.accepted);
    assert(controller.state() == media_sdk::runtime::RuntimePlaybackState::FallbackPending);
}

void staleGenerationFailureIsCountedButDoesNotFallback()
{
    media_sdk::runtime::NativeFallbackController controller;
    controller.reset(10, 4, media_sdk::runtime::VideoOutputPolicy::PreferNative);

    const auto oldGeneration = controller.beginFallback(request(10, 3));
    assert(!oldGeneration.accepted);
    assert(controller.state() == media_sdk::runtime::RuntimePlaybackState::Playing);
    assert(controller.policy() == media_sdk::runtime::VideoOutputPolicy::PreferNative);

    const auto wrongSession = controller.beginFallback(request(11, 4));
    assert(!wrongSession.accepted);
    assert(controller.state() == media_sdk::runtime::RuntimePlaybackState::Playing);
}

void fallbackSwitchesCurrentSessionToCpuOnly()
{
    media_sdk::runtime::NativeFallbackController controller;
    controller.reset(10, 4, media_sdk::runtime::VideoOutputPolicy::PreferNative);

    const auto transition = controller.beginFallback(request(10, 4));
    assert(transition.accepted);
    assert(transition.newPolicy == media_sdk::runtime::VideoOutputPolicy::CpuOnly);
    assert(controller.policy() == media_sdk::runtime::VideoOutputPolicy::CpuOnly);
}

void fallbackIncrementsGenerationAndRequiresSeekCompletionBeforeResume()
{
    media_sdk::runtime::NativeFallbackController controller;
    controller.reset(10, 4, media_sdk::runtime::VideoOutputPolicy::PreferNative);

    const auto transition = controller.beginFallback(request(10, 4));
    assert(transition.accepted);
    assert(transition.newGeneration == 5);
    assert(controller.state() == media_sdk::runtime::RuntimePlaybackState::FallbackPending);

    assert(!controller.completeSeek(10, 4));
    assert(controller.state() == media_sdk::runtime::RuntimePlaybackState::FallbackPending);

    assert(controller.completeSeek(10, 5));
    assert(controller.state() == media_sdk::runtime::RuntimePlaybackState::Playing);
}

void fallbackRequiresAudioPauseFlushQueueAbortAndPresenterClear()
{
    media_sdk::runtime::NativeFallbackController controller;
    controller.reset(10, 4, media_sdk::runtime::VideoOutputPolicy::PreferNative);

    const auto transition = controller.beginFallback(request(10, 4));
    assert(transition.accepted);
    assert(transition.pauseAudio);
    assert(transition.flushAudio);
    assert(transition.abortQueues);
    assert(transition.clearPresenter);
    assert(transition.requestCpuDecode);
}

} // namespace

int main()
{
    currentGenerationNativeFailureStartsFallbackPending();
    staleGenerationFailureIsCountedButDoesNotFallback();
    fallbackSwitchesCurrentSessionToCpuOnly();
    fallbackIncrementsGenerationAndRequiresSeekCompletionBeforeResume();
    fallbackRequiresAudioPauseFlushQueueAbortAndPresenterClear();
}
