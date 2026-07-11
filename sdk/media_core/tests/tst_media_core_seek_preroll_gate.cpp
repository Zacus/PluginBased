#include "SeekPrerollGate.h"

#include <cassert>
#include <chrono>

using namespace std::chrono_literals;

namespace {

void videoBeforeTargetIsDiscarded()
{
    media_sdk::SeekPrerollGate gate({
        .target = 1000ms,
        .generation = 2,
        .hasVideo = true,
        .hasAudio = false,
    });

    const auto decision = gate.inspectVideo(900ms, 2);

    assert(decision.action == media_sdk::SeekPrerollAction::Discard);
    assert(!gate.shouldEmitCompletion());
}

void videoAtTargetIsAcceptedAndCompletesVideoOnlySeek()
{
    media_sdk::SeekPrerollGate gate({
        .target = 1000ms,
        .generation = 2,
        .hasVideo = true,
        .hasAudio = false,
    });

    const auto decision = gate.inspectVideo(1000ms, 2);
    assert(decision.action == media_sdk::SeekPrerollAction::Accept);
    gate.markVideoAccepted();

    assert(gate.shouldEmitCompletion());
    assert(gate.completionPosition() == 1000ms);
}

void staleGenerationDoesNotCompleteSeek()
{
    media_sdk::SeekPrerollGate gate({
        .target = 1000ms,
        .generation = 2,
        .hasVideo = true,
        .hasAudio = false,
    });

    const auto decision = gate.inspectVideo(1200ms, 1);

    assert(decision.action == media_sdk::SeekPrerollAction::Stale);
    assert(!gate.shouldEmitCompletion());
}

void missingVideoPtsIsDiscardedEvenAtZeroTarget()
{
    media_sdk::SeekPrerollGate gate({
        .target = 0ms,
        .generation = 2,
        .hasVideo = true,
        .hasAudio = false,
    });

    const auto decision = gate.inspectMissingVideoPts(2);

    assert(decision.action == media_sdk::SeekPrerollAction::Discard);
    assert(!gate.shouldEmitCompletion());
}

void audioOnlyAcceptedFrameCompletesSeek()
{
    media_sdk::SeekPrerollGate gate({
        .target = 500ms,
        .generation = 4,
        .hasVideo = false,
        .hasAudio = true,
    });

    const auto decision = gate.inspectAudio(510ms, 4);
    assert(decision.action == media_sdk::SeekPrerollAction::Accept);
    gate.markAudioAccepted();

    assert(gate.shouldEmitCompletion());
}

void discardLimitAllowsCompletionFallback()
{
    media_sdk::SeekPrerollGate gate({
        .target = 5000ms,
        .generation = 3,
        .hasVideo = true,
        .hasAudio = false,
        .maxDiscardedVideoFrames = 2,
    });

    assert(gate.inspectVideo(1000ms, 3).action == media_sdk::SeekPrerollAction::Discard);
    gate.markVideoDiscarded();
    assert(!gate.discardLimitReached());

    assert(gate.inspectVideo(2000ms, 3).action == media_sdk::SeekPrerollAction::Discard);
    gate.markVideoDiscarded();

    assert(gate.discardLimitReached());
}

void videoTailFallbackIsOptIn()
{
    media_sdk::SeekPrerollGate pausedGate({
        .target = 5000ms,
        .generation = 3,
        .hasVideo = true,
    });
    assert(!pausedGate.allowsVideoTailFallback());

    media_sdk::SeekPrerollGate playingGate({
        .target = 5000ms,
        .generation = 3,
        .hasVideo = true,
        .allowVideoTailFallback = true,
    });
    assert(playingGate.allowsVideoTailFallback());
}

void audioVideoSeekCompletesOnVideoButRetiresAfterAudio()
{
    media_sdk::SeekPrerollGate gate({
        .target = 1000ms,
        .generation = 5,
        .hasVideo = true,
        .hasAudio = true,
    });

    assert(gate.inspectVideo(1000ms, 5).action == media_sdk::SeekPrerollAction::Accept);
    gate.markVideoAccepted();

    assert(gate.shouldEmitCompletion());
    assert(!gate.readyToRetire());

    gate.markCompletionSent();
    assert(gate.completionSent());
    assert(!gate.shouldEmitCompletion());
    assert(gate.inspectAudio(900ms, 5).action == media_sdk::SeekPrerollAction::Discard);

    assert(gate.inspectAudio(1000ms, 5).action == media_sdk::SeekPrerollAction::Accept);
    gate.markAudioAccepted();

    assert(gate.readyToRetire());
}

void playingAudioVideoSeekCanCompleteOnAudioMasterBeforeVideo()
{
    media_sdk::SeekPrerollGate gate({
        .target = 1000ms,
        .generation = 6,
        .hasVideo = true,
        .hasAudio = true,
        .preferAudioCompletion = true,
    });

    assert(gate.inspectAudio(1000ms, 6).action == media_sdk::SeekPrerollAction::Accept);
    gate.markAudioAccepted();

    assert(gate.shouldEmitCompletion());
    assert(!gate.readyToRetire());

    gate.markCompletionSent();
    assert(!gate.shouldEmitCompletion());

    assert(gate.inspectVideo(1000ms, 6).action == media_sdk::SeekPrerollAction::Accept);
    gate.markVideoAccepted();

    assert(gate.readyToRetire());
}

void playingAudioVideoSeekStillCompletesOnVideoWhenVideoArrivesFirst()
{
    media_sdk::SeekPrerollGate gate({
        .target = 1000ms,
        .generation = 7,
        .hasVideo = true,
        .hasAudio = true,
        .preferAudioCompletion = true,
    });

    assert(gate.inspectVideo(1000ms, 7).action == media_sdk::SeekPrerollAction::Accept);
    gate.markVideoAccepted();

    assert(gate.shouldEmitCompletion());
    assert(!gate.readyToRetire());

    gate.markCompletionSent();
    assert(!gate.shouldEmitCompletion());

    assert(gate.inspectAudio(1000ms, 7).action == media_sdk::SeekPrerollAction::Accept);
    gate.markAudioAccepted();

    assert(gate.readyToRetire());
}

} // namespace

int main()
{
    videoBeforeTargetIsDiscarded();
    videoAtTargetIsAcceptedAndCompletesVideoOnlySeek();
    staleGenerationDoesNotCompleteSeek();
    missingVideoPtsIsDiscardedEvenAtZeroTarget();
    audioOnlyAcceptedFrameCompletesSeek();
    discardLimitAllowsCompletionFallback();
    videoTailFallbackIsOptIn();
    audioVideoSeekCompletesOnVideoButRetiresAfterAudio();
    playingAudioVideoSeekCanCompleteOnAudioMasterBeforeVideo();
    playingAudioVideoSeekStillCompletesOnVideoWhenVideoArrivesFirst();
}
