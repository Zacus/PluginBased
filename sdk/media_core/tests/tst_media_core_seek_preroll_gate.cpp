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

} // namespace

int main()
{
    videoBeforeTargetIsDiscarded();
    videoAtTargetIsAcceptedAndCompletesVideoOnlySeek();
    staleGenerationDoesNotCompleteSeek();
    audioOnlyAcceptedFrameCompletesSeek();
}
