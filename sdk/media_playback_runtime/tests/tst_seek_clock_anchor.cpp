#include "SeekClockAnchor.h"

#include <cassert>
#include <chrono>

using namespace std::chrono_literals;

namespace {

void boundedForwardGapRequestsSilenceFill()
{
    media_sdk::runtime::SeekClockAnchor anchor;
    anchor.begin(2, 4763ms, 2000ms);

    const auto decision = anchor.inspectFirstAudio(2, 6035ms);

    assert(decision.shouldFill);
    assert(!decision.exceedsMaxGap);
    assert(decision.target == 4763ms);
    assert(decision.firstAudioPts == 6035ms);
    assert(decision.gap == 1272ms);
    assert(decision.generation == 2);
}

void largeGapIsReportedButNotFilled()
{
    media_sdk::runtime::SeekClockAnchor anchor;
    anchor.begin(2, 1000ms, 2000ms);

    const auto decision = anchor.inspectFirstAudio(2, 5000ms);

    assert(!decision.shouldFill);
    assert(decision.exceedsMaxGap);
    assert(decision.gap == 4000ms);
}

void backwardOrExactAudioDoesNotFill()
{
    media_sdk::runtime::SeekClockAnchor anchor;
    anchor.begin(2, 5000ms, 2000ms);

    const auto decision = anchor.inspectFirstAudio(2, 4999ms);

    assert(!decision.shouldFill);
    assert(!decision.exceedsMaxGap);
    assert(decision.gap == -1ms);
}

void staleGenerationDoesNotConsumeAnchor()
{
    media_sdk::runtime::SeekClockAnchor anchor;
    anchor.begin(2, 4763ms, 2000ms);

    const auto stale = anchor.inspectFirstAudio(1, 6035ms);
    assert(!stale.shouldFill);
    assert(stale.generation == 0);

    const auto current = anchor.inspectFirstAudio(2, 6035ms);
    assert(current.shouldFill);
}

void firstCurrentAudioConsumesAnchorOnce()
{
    media_sdk::runtime::SeekClockAnchor anchor;
    anchor.begin(2, 4763ms, 2000ms);

    const auto first = anchor.inspectFirstAudio(2, 6035ms);
    const auto second = anchor.inspectFirstAudio(2, 6500ms);

    assert(first.shouldFill);
    assert(!second.shouldFill);
    assert(second.generation == 0);
}

} // namespace

int main()
{
    boundedForwardGapRequestsSilenceFill();
    largeGapIsReportedButNotFilled();
    backwardOrExactAudioDoesNotFill();
    staleGenerationDoesNotConsumeAnchor();
    firstCurrentAudioConsumesAnchorOnce();
}
