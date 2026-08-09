#include "playback/PlaybackSeek.h"

#include <cassert>
#include <limits>

void forwardMovesByRequestedDelta()
{
    const auto target = calculateRelativeSeekTarget(12'000, 60'000, 10'000);
    assert(target == 22'000);
}

void forwardClampsToDurationAndStopsAtEnd()
{
    const auto target = calculateRelativeSeekTarget(55'000, 60'000, 10'000);
    assert(target == 60'000);
    assert(!calculateRelativeSeekTarget(*target, 60'000, 10'000));
}

void invalidDurationAndZeroDeltaAreRejected()
{
    assert(!calculateRelativeSeekTarget(0, 0, 10'000));
    assert(!calculateRelativeSeekTarget(10'000, 60'000, 0));
}

void forwardAvailabilityIncludesLogicalCompletion()
{
    assert(isForwardSeekAvailable(true, 12'000, 60'000, false));
    assert(!isForwardSeekAvailable(false, 12'000, 60'000, false));
    assert(!isForwardSeekAvailable(true, 12'000, 0, false));
    assert(!isForwardSeekAvailable(true, 60'000, 60'000, false));
    assert(!isForwardSeekAvailable(true, 59'500, 60'000, true));
}

void negativeDeltaUsesTheSameBoundaryContract()
{
    assert(calculateRelativeSeekTarget(5'000, 60'000, -10'000) == 0);
    assert(!calculateRelativeSeekTarget(0, 60'000, -10'000));
}

void extremeValuesDoNotOverflow()
{
    constexpr qint64 maximum = std::numeric_limits<qint64>::max();
    constexpr qint64 minimum = std::numeric_limits<qint64>::min();
    assert(calculateRelativeSeekTarget(maximum - 5, maximum, maximum) == maximum);
    assert(!calculateRelativeSeekTarget(maximum, maximum, 1));
    assert(calculateRelativeSeekTarget(maximum, maximum, minimum) == 0);
}

void latestSeekOwnsPendingStateUntilItsCompletion()
{
    PlaybackSeekState state;
    const int firstGeneration = state.begin(22'000);
    assert(state.basePosition(12'000) == 22'000);
    assert(!state.acceptsPositionUpdate());

    const int secondGeneration = state.begin(32'000);
    assert(state.basePosition(12'000) == 32'000);
    const bool staleCompletionAccepted = state.complete(firstGeneration);
    assert(!staleCompletionAccepted);
    assert(!state.acceptsPositionUpdate());
    assert(state.basePosition(12'000) == 32'000);

    const bool latestCompletionAccepted = state.complete(secondGeneration);
    assert(latestCompletionAccepted);
    assert(state.acceptsPositionUpdate());
    assert(state.basePosition(12'000) == 12'000);
}

void resetCancelsPendingSeekAndRestoresPositionUpdates()
{
    PlaybackSeekState state;
    const int oldGeneration = state.begin(22'000);

    state.reset();

    assert(state.acceptsPositionUpdate());
    assert(state.basePosition(12'000) == 12'000);
    const int newGeneration = state.begin(42'000);
    assert(newGeneration != oldGeneration);
    assert(!state.isPending(oldGeneration));
    assert(state.isPending(newGeneration));
}

void resetInvalidatesInFlightGeneration()
{
    PlaybackSeekState state;
    const int generation = state.begin(22'000);
    assert(state.isPending(generation));

    state.reset();

    assert(!state.isPending(generation));
}

void resetVersionChangesOnlyForReset()
{
    PlaybackSeekState state;
    const quint64 initialVersion = state.resetVersion();

    state.begin(22'000);
    state.begin(32'000);
    assert(state.resetVersion() == initialVersion);

    assert(state.complete(2));
    assert(state.resetVersion() == initialVersion);

    state.reset();
    assert(state.resetVersion() != initialVersion);
}

int main()
{
    forwardMovesByRequestedDelta();
    forwardClampsToDurationAndStopsAtEnd();
    invalidDurationAndZeroDeltaAreRejected();
    forwardAvailabilityIncludesLogicalCompletion();
    negativeDeltaUsesTheSameBoundaryContract();
    extremeValuesDoNotOverflow();
    latestSeekOwnsPendingStateUntilItsCompletion();
    resetCancelsPendingSeekAndRestoresPositionUpdates();
    resetInvalidatesInFlightGeneration();
    resetVersionChangesOnlyForReset();
    return 0;
}
