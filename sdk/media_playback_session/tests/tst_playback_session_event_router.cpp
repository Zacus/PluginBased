#include "SessionEventRouter.h"

#include "media_sdk/runtime/AudioOutput.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

using namespace std::chrono_literals;

namespace {

media_sdk::EventMetadata coreTimeline(std::uint64_t sessionId, std::uint64_t generation)
{
    return {
        .sessionId = sessionId,
        .generation = generation,
    };
}

media_sdk::runtime::RuntimeTimeline runtimeTimeline(std::uint64_t sessionId,
                                                    std::uint64_t generation)
{
    return {
        .sessionId = sessionId,
        .generation = generation,
    };
}

media_sdk::PlayerEvent mediaInfoEvent(media_sdk::EventMetadata metadata)
{
    return {
        .metadata = metadata,
        .payload = media_sdk::MediaInfoEvent {
            .info = media_sdk::MediaInfo {
                .duration = 2000ms,
                .width = 3840,
                .height = 2160,
                .fps = 60.0,
                .sampleRate = 48000,
                .channels = 2,
                .formatName = "mov",
            },
        },
    };
}

media_sdk::PlayerEvent positionEvent(media_sdk::EventMetadata metadata,
                                     std::chrono::milliseconds position)
{
    return {
        .metadata = metadata,
        .payload = media_sdk::PositionChangedEvent {
            .position = position,
        },
    };
}

media_sdk::PlayerEvent seekCompletedEvent(media_sdk::EventMetadata metadata,
                                          std::chrono::milliseconds position,
                                          std::chrono::milliseconds requestedPosition = -1ms,
                                          media_sdk::SeekRequestId requestId = 0)
{
    if (requestedPosition < 0ms)
        requestedPosition = position;
    return {
        .metadata = metadata,
        .payload = media_sdk::SeekCompletedEvent {
            .position = position,
            .requestedPosition = requestedPosition,
            .requestId = requestId,
        },
    };
}

media_sdk::PlayerEvent eofEvent(media_sdk::EventMetadata metadata)
{
    return {
        .metadata = metadata,
        .payload = media_sdk::EndOfFileEvent {},
    };
}

media_sdk::PlayerEvent errorEvent(media_sdk::EventMetadata metadata)
{
    return {
        .metadata = metadata,
        .payload = media_sdk::ErrorEvent {
            .error = media_sdk::MediaError {
                .code = media_sdk::MediaErrorCode::DecodeFailed,
                .message = "decode failed",
            },
        },
    };
}

struct RecordingRuntimeControl {
    int openCount = 0;
    int completeSeekCount = 0;
    int eofCount = 0;
    media_sdk::MediaInfo lastMediaInfo {};
    media_sdk::runtime::RuntimeTimeline nextOpenTimeline = runtimeTimeline(20, 7);
    media_sdk::runtime::RuntimeTimeline lastCompletedSeek {};
    media_sdk::runtime::RuntimeTimeline lastEof {};
    media_sdk::runtime::ClockSnapshot nextClock {
        .position = 0ms,
        .generation = 7,
        .valid = true,
    };

    [[nodiscard("runtime open result controls whether the session can accept frames")]]
    media_sdk::Result<media_sdk::runtime::RuntimeTimeline> openRuntimeForMedia(
        const media_sdk::MediaInfo& info)
    {
        ++openCount;
        lastMediaInfo = info;
        return media_sdk::Result<media_sdk::runtime::RuntimeTimeline>::success(nextOpenTimeline);
    }

    void completeSeek(media_sdk::runtime::RuntimeTimeline timeline)
    {
        ++completeSeekCount;
        lastCompletedSeek = timeline;
    }

    void enqueueEndOfStream(media_sdk::runtime::RuntimeTimeline timeline)
    {
        ++eofCount;
        lastEof = timeline;
    }

    std::optional<media_sdk::runtime::ClockSnapshot> playbackClock(
        media_sdk::runtime::RuntimeTimeline timeline)
    {
        if (nextClock.generation != timeline.generation)
            return std::nullopt;
        return nextClock;
    }
};

class RecordingSessionEvents final : public media_sdk::session::ISessionEvents {
public:
    RecordingSessionEvents(media_sdk::session::SessionTimeline* timeline = nullptr,
                           std::optional<media_sdk::EventMetadata> expectedMediaInfoTimeline = std::nullopt)
        : m_timeline(timeline)
        , m_expectedMediaInfoTimeline(expectedMediaInfoTimeline)
    {
    }

    void onEvent(const media_sdk::PlayerEvent& event) override
    {
        if (std::holds_alternative<media_sdk::MediaInfoEvent>(event.payload)
            && m_timeline
            && m_expectedMediaInfoTimeline.has_value()) {
            mediaInfoSawAcceptedTimeline =
                m_timeline->runtimeForCoreEvent(*m_expectedMediaInfoTimeline).has_value();
        }
        events.push_back(event);
    }

    std::vector<media_sdk::PlayerEvent> events;
    bool mediaInfoSawAcceptedTimeline = false;

private:
    media_sdk::session::SessionTimeline* m_timeline = nullptr;
    std::optional<media_sdk::EventMetadata> m_expectedMediaInfoTimeline;
};

void mediaInfoOpensRuntimeBeforeExternalFrameAcceptance()
{
    media_sdk::session::SessionTimeline timeline;
    RecordingRuntimeControl runtime;
    RecordingSessionEvents events(&timeline, coreTimeline(10, 3));
    media_sdk::session::SessionEventRouter<RecordingRuntimeControl> router(
        runtime,
        timeline,
        &events);

    router.beginOpen();
    router.onEvent(mediaInfoEvent(coreTimeline(10, 3)));

    assert(runtime.openCount == 1);
    assert(runtime.lastMediaInfo.width == 3840);
    assert(events.events.size() == 1);
    assert(std::holds_alternative<media_sdk::MediaInfoEvent>(events.events.back().payload));
    assert(events.mediaInfoSawAcceptedTimeline);

    const auto acceptedRuntime = timeline.runtimeForCoreEvent(coreTimeline(10, 3));
    assert(acceptedRuntime.has_value());
    assert(acceptedRuntime->sessionId == 20);
    assert(acceptedRuntime->generation == 7);
}

void positionChangedIsForwardedOnlyForAcceptedCoreTimeline()
{
    media_sdk::session::SessionTimeline timeline;
    RecordingRuntimeControl runtime;
    RecordingSessionEvents events;
    media_sdk::session::SessionEventRouter<RecordingRuntimeControl> router(
        runtime,
        timeline,
        &events);

    router.beginOpen();
    router.onEvent(mediaInfoEvent(coreTimeline(10, 3)));
    events.events.clear();

    router.onEvent(positionEvent(coreTimeline(9, 3), 100ms));
    router.onEvent(positionEvent(coreTimeline(10, 4), 200ms));
    assert(events.events.empty());

    runtime.nextClock.position = 300ms;
    router.onEvent(positionEvent(coreTimeline(10, 3), 300ms));
    assert(events.events.size() == 1);
    const auto* position = std::get_if<media_sdk::PositionChangedEvent>(&events.events.back().payload);
    assert(position);
    assert(position->position == 300ms);
}

void runtimeClockTickForwardsPositionForAcceptedTimeline()
{
    media_sdk::session::SessionTimeline timeline;
    RecordingRuntimeControl runtime;
    RecordingSessionEvents events;
    media_sdk::session::SessionEventRouter<RecordingRuntimeControl> router(
        runtime,
        timeline,
        &events);

    router.beginOpen();
    router.onEvent(mediaInfoEvent(coreTimeline(10, 3)));
    events.events.clear();

    router.onPlaybackClockTick(runtimeTimeline(19, 7), {
        .position = 23000ms,
        .generation = 7,
        .valid = true,
    });
    assert(events.events.empty());

    router.onPlaybackClockTick(runtimeTimeline(20, 7), {
        .position = 23000ms,
        .generation = 7,
        .valid = true,
    });
    assert(events.events.size() == 1);
    const auto* firstPosition =
        std::get_if<media_sdk::PositionChangedEvent>(&events.events.back().payload);
    assert(firstPosition);
    assert(firstPosition->position == 23000ms);

    router.onPlaybackClockTick(runtimeTimeline(20, 7), {
        .position = 23000ms,
        .generation = 7,
        .valid = true,
    });
    assert(events.events.size() == 1);

    router.onPlaybackClockTick(runtimeTimeline(20, 7), {
        .position = 23250ms,
        .generation = 7,
        .valid = true,
    });
    assert(events.events.size() == 2);
    const auto* secondPosition =
        std::get_if<media_sdk::PositionChangedEvent>(&events.events.back().payload);
    assert(secondPosition);
    assert(secondPosition->position == 23250ms);
}

void seekCompletedCompletesRuntimeTimelineAndResumesFrameAcceptance()
{
    media_sdk::session::SessionTimeline timeline;
    RecordingRuntimeControl runtime;
    RecordingSessionEvents events;
    media_sdk::session::SessionEventRouter<RecordingRuntimeControl> router(
        runtime,
        timeline,
        &events);

    router.beginOpen();
    router.onEvent(mediaInfoEvent(coreTimeline(10, 3)));
    router.beginSeek(runtimeTimeline(21, 8));
    events.events.clear();

    assert(!timeline.runtimeForCoreEvent(coreTimeline(10, 3)).has_value());

    router.onEvent(seekCompletedEvent(coreTimeline(10, 4), 1200ms));

    assert(runtime.completeSeekCount == 1);
    assert(runtime.lastCompletedSeek.sessionId == 21);
    assert(runtime.lastCompletedSeek.generation == 8);
    assert(events.events.size() == 1);
    assert(std::holds_alternative<media_sdk::SeekCompletedEvent>(events.events.back().payload));

    const auto acceptedRuntime = timeline.runtimeForCoreEvent(coreTimeline(10, 4));
    assert(acceptedRuntime.has_value());
    assert(acceptedRuntime->sessionId == 21);
    assert(acceptedRuntime->generation == 8);
}

void rateChangeCompletionEmitsRateEventInsteadOfSeekEvent()
{
    media_sdk::session::SessionTimeline timeline;
    RecordingRuntimeControl runtime;
    RecordingSessionEvents events;
    media_sdk::session::SessionEventRouter<RecordingRuntimeControl> router(
        runtime,
        timeline,
        &events);

    router.beginOpen();
    router.onEvent(mediaInfoEvent(coreTimeline(10, 3)));
    router.beginRateChange(runtimeTimeline(21, 8), 1200ms, 1.5);
    events.events.clear();

    router.onEvent(seekCompletedEvent(coreTimeline(10, 4), 1200ms));

    assert(runtime.completeSeekCount == 1);
    assert(events.events.size() == 1);
    const auto* rate = std::get_if<media_sdk::PlaybackRateChangedEvent>(
        &events.events.back().payload);
    assert(rate);
    assert(rate->playbackRate == 1.5);
    assert(rate->position == 1200ms);
}

void supersededSeekCompletionCannotCommitLatestRateTimeline()
{
    media_sdk::session::SessionTimeline timeline;
    RecordingRuntimeControl runtime;
    RecordingSessionEvents events;
    media_sdk::session::SessionEventRouter<RecordingRuntimeControl> router(
        runtime,
        timeline,
        &events);

    router.beginOpen();
    router.onEvent(mediaInfoEvent(coreTimeline(10, 3)));
    router.beginRateChange(runtimeTimeline(21, 8), 1000ms, 1.25);
    router.beginRateChange(runtimeTimeline(21, 9), 1400ms, 1.5);
    events.events.clear();

    router.onEvent(seekCompletedEvent(coreTimeline(10, 4), 1000ms, 1000ms));
    assert(runtime.completeSeekCount == 0);
    assert(events.events.empty());
    assert(!timeline.hasAcceptedTimeline());

    router.onEvent(seekCompletedEvent(coreTimeline(10, 5), 1400ms, 1400ms));
    assert(runtime.completeSeekCount == 1);
    assert(runtime.lastCompletedSeek.generation == 9);
    assert(events.events.size() == 1);
    const auto* rate = std::get_if<media_sdk::PlaybackRateChangedEvent>(
        &events.events.back().payload);
    assert(rate && rate->playbackRate == 1.5);
}

void supersededSamePositionCompletionCannotCommitLatestRateTimeline()
{
    media_sdk::session::SessionTimeline timeline;
    RecordingRuntimeControl runtime;
    RecordingSessionEvents events;
    media_sdk::session::SessionEventRouter<RecordingRuntimeControl> router(
        runtime,
        timeline,
        &events);

    router.beginOpen();
    router.onEvent(mediaInfoEvent(coreTimeline(10, 3)));
    router.beginRateChange(runtimeTimeline(21, 8), 1000ms, 1.25, 41);
    router.beginRateChange(runtimeTimeline(21, 9), 1000ms, 1.5, 42);
    events.events.clear();

    router.onEvent(seekCompletedEvent(coreTimeline(10, 4), 1000ms, 1000ms, 41));
    assert(runtime.completeSeekCount == 0);
    assert(events.events.empty());

    router.onEvent(seekCompletedEvent(coreTimeline(10, 5), 1000ms, 1000ms, 42));
    assert(runtime.completeSeekCount == 1);
    const auto* rate = std::get_if<media_sdk::PlaybackRateChangedEvent>(
        &events.events.back().payload);
    assert(rate && rate->playbackRate == 1.5);
}

void seekForwardsRuntimeClockPositionsInsteadOfDecoderPositions()
{
    media_sdk::session::SessionTimeline timeline;
    RecordingRuntimeControl runtime;
    RecordingSessionEvents events;
    media_sdk::session::SessionEventRouter<RecordingRuntimeControl> router(
        runtime,
        timeline,
        &events);

    router.beginOpen();
    router.onEvent(mediaInfoEvent(coreTimeline(10, 3)));
    router.beginSeek(runtimeTimeline(21, 8), 1200ms);
    runtime.nextClock.generation = 8;
    events.events.clear();

    router.onEvent(seekCompletedEvent(coreTimeline(10, 4), 1200ms));
    assert(events.events.size() == 1);
    assert(std::holds_alternative<media_sdk::SeekCompletedEvent>(events.events.back().payload));

    runtime.nextClock.position = 1100ms;
    router.onEvent(positionEvent(coreTimeline(10, 4), 2200ms));
    assert(events.events.size() == 1);

    runtime.nextClock.position = 1210ms;
    router.onEvent(positionEvent(coreTimeline(10, 4), 2300ms));
    assert(events.events.size() == 2);
    const auto* firstPosition =
        std::get_if<media_sdk::PositionChangedEvent>(&events.events.back().payload);
    assert(firstPosition);
    assert(firstPosition->position == 1210ms);

    router.onEvent(positionEvent(coreTimeline(10, 4), 2400ms));
    assert(events.events.size() == 2);

    runtime.nextClock.position = 1300ms;
    router.onEvent(positionEvent(coreTimeline(10, 4), 2500ms));
    assert(events.events.size() == 3);
    const auto* secondPosition =
        std::get_if<media_sdk::PositionChangedEvent>(&events.events.back().payload);
    assert(secondPosition);
    assert(secondPosition->position == 1300ms);
}

void seekForwardsAnchoredRuntimeClockAtTarget()
{
    media_sdk::session::SessionTimeline timeline;
    RecordingRuntimeControl runtime;
    RecordingSessionEvents events;
    media_sdk::session::SessionEventRouter<RecordingRuntimeControl> router(
        runtime,
        timeline,
        &events);

    router.onEvent(mediaInfoEvent(coreTimeline(10, 3)));
    router.beginSeek(runtimeTimeline(21, 8), 4763ms);
    runtime.nextClock.generation = 8;
    events.events.clear();

    router.onEvent(seekCompletedEvent(coreTimeline(10, 4), 4763ms));
    assert(events.events.size() == 1);

    runtime.nextClock.position = 4763ms;
    router.onEvent(positionEvent(coreTimeline(10, 4), 6035ms));

    assert(events.events.size() == 2);
    const auto* position =
        std::get_if<media_sdk::PositionChangedEvent>(&events.events.back().payload);
    assert(position);
    assert(position->position == 4763ms);
}

void coreEndOfFileEnqueuesRuntimeEofWithoutExternalEof()
{
    media_sdk::session::SessionTimeline timeline;
    RecordingRuntimeControl runtime;
    RecordingSessionEvents events;
    media_sdk::session::SessionEventRouter<RecordingRuntimeControl> router(
        runtime,
        timeline,
        &events);

    router.beginOpen();
    router.onEvent(mediaInfoEvent(coreTimeline(10, 3)));
    events.events.clear();

    router.onEvent(eofEvent(coreTimeline(10, 3)));

    assert(runtime.eofCount == 1);
    assert(runtime.lastEof.sessionId == 20);
    assert(runtime.lastEof.generation == 7);
    assert(events.events.empty());
}

void runtimeEndOfStreamPresentedEmitsExternalEof()
{
    media_sdk::session::SessionTimeline timeline;
    RecordingRuntimeControl runtime;
    RecordingSessionEvents events;
    media_sdk::session::SessionEventRouter<RecordingRuntimeControl> router(
        runtime,
        timeline,
        &events);

    router.beginOpen();
    router.onEvent(mediaInfoEvent(coreTimeline(10, 3)));
    router.onEvent(eofEvent(coreTimeline(10, 3)));
    events.events.clear();

    router.onEndOfStreamPresented(runtimeTimeline(19, 7));
    assert(events.events.empty());

    router.onEndOfStreamPresented(runtimeTimeline(20, 7));
    assert(events.events.size() == 2);
    assert(std::holds_alternative<media_sdk::PositionChangedEvent>(events.events[0].payload));
    assert(events.events[0].metadata.sessionId == 10);
    assert(events.events[0].metadata.generation == 3);
    assert(std::holds_alternative<media_sdk::EndOfFileEvent>(events.events.back().payload));
    assert(events.events.back().metadata.sessionId == 10);
    assert(events.events.back().metadata.generation == 3);

    router.onEndOfStreamPresented(runtimeTimeline(20, 7));
    assert(events.events.size() == 2);
}

void runtimeEndOfStreamPresentedForwardsFinalRuntimeClockBeforeExternalEof()
{
    media_sdk::session::SessionTimeline timeline;
    RecordingRuntimeControl runtime;
    RecordingSessionEvents events;
    media_sdk::session::SessionEventRouter<RecordingRuntimeControl> router(
        runtime,
        timeline,
        &events);

    router.beginOpen();
    router.onEvent(mediaInfoEvent(coreTimeline(10, 3)));
    runtime.nextClock.position = 22769ms;
    router.onEvent(positionEvent(coreTimeline(10, 3), 22000ms));
    router.onEvent(eofEvent(coreTimeline(10, 3)));
    events.events.clear();

    runtime.nextClock.position = 24337ms;
    router.onEndOfStreamPresented(runtimeTimeline(20, 7));

    assert(events.events.size() == 2);
    const auto* finalPosition =
        std::get_if<media_sdk::PositionChangedEvent>(&events.events[0].payload);
    assert(finalPosition);
    assert(finalPosition->position == 24337ms);
    assert(std::holds_alternative<media_sdk::EndOfFileEvent>(events.events[1].payload));
}

void runtimeEndOfStreamPresentedWithoutCoreEofIsIgnored()
{
    media_sdk::session::SessionTimeline timeline;
    RecordingRuntimeControl runtime;
    RecordingSessionEvents events;
    media_sdk::session::SessionEventRouter<RecordingRuntimeControl> router(
        runtime,
        timeline,
        &events);

    router.beginOpen();
    router.onEvent(mediaInfoEvent(coreTimeline(10, 3)));
    events.events.clear();

    router.onEndOfStreamPresented(runtimeTimeline(20, 7));

    assert(events.events.empty());
}

void beginSeekClearsQueuedCoreEofUntilNewRuntimeDrain()
{
    media_sdk::session::SessionTimeline timeline;
    RecordingRuntimeControl runtime;
    RecordingSessionEvents events;
    media_sdk::session::SessionEventRouter<RecordingRuntimeControl> router(
        runtime,
        timeline,
        &events);

    router.beginOpen();
    router.onEvent(mediaInfoEvent(coreTimeline(10, 3)));
    router.onEvent(eofEvent(coreTimeline(10, 3)));
    assert(runtime.eofCount == 1);

    router.beginSeek(runtimeTimeline(21, 8));
    router.onEndOfStreamPresented(runtimeTimeline(20, 7));
    assert(events.events.size() == 1);
    assert(std::holds_alternative<media_sdk::MediaInfoEvent>(events.events.back().payload));

    events.events.clear();
    router.onEvent(seekCompletedEvent(coreTimeline(10, 4), 1200ms));
    router.onEvent(eofEvent(coreTimeline(10, 4)));
    assert(runtime.eofCount == 2);
    router.onEndOfStreamPresented(runtimeTimeline(21, 8));
    assert(events.events.size() == 2);
    assert(std::holds_alternative<media_sdk::EndOfFileEvent>(events.events.back().payload));
}

void cancelFrameAcceptanceDropsPendingSeekCompletion()
{
    media_sdk::session::SessionTimeline timeline;
    RecordingRuntimeControl runtime;
    RecordingSessionEvents events;
    media_sdk::session::SessionEventRouter<RecordingRuntimeControl> router(
        runtime,
        timeline,
        &events);

    router.beginOpen();
    router.onEvent(mediaInfoEvent(coreTimeline(10, 3)));
    router.beginSeek(runtimeTimeline(21, 8));
    router.cancelFrameAcceptance();
    events.events.clear();

    router.onEvent(seekCompletedEvent(coreTimeline(10, 4), 1200ms));

    assert(runtime.completeSeekCount == 0);
    assert(events.events.empty());
    assert(!timeline.hasAcceptedTimeline());
}

void staleErrorIsIgnoredAfterTimelineIsAccepted()
{
    media_sdk::session::SessionTimeline timeline;
    RecordingRuntimeControl runtime;
    RecordingSessionEvents events;
    media_sdk::session::SessionEventRouter<RecordingRuntimeControl> router(
        runtime,
        timeline,
        &events);

    router.beginOpen();
    router.onEvent(errorEvent(coreTimeline(1, 1)));
    assert(events.events.size() == 1);
    assert(std::holds_alternative<media_sdk::ErrorEvent>(events.events.back().payload));

    router.beginOpen();
    router.onEvent(mediaInfoEvent(coreTimeline(10, 3)));
    events.events.clear();

    router.onEvent(errorEvent(coreTimeline(10, 2)));
    assert(events.events.empty());

    router.onEvent(errorEvent(coreTimeline(10, 3)));
    assert(events.events.size() == 1);
    assert(std::holds_alternative<media_sdk::ErrorEvent>(events.events.back().payload));
}

} // namespace

int main()
{
    mediaInfoOpensRuntimeBeforeExternalFrameAcceptance();
    positionChangedIsForwardedOnlyForAcceptedCoreTimeline();
    runtimeClockTickForwardsPositionForAcceptedTimeline();
    seekCompletedCompletesRuntimeTimelineAndResumesFrameAcceptance();
    rateChangeCompletionEmitsRateEventInsteadOfSeekEvent();
    supersededSeekCompletionCannotCommitLatestRateTimeline();
    supersededSamePositionCompletionCannotCommitLatestRateTimeline();
    seekForwardsRuntimeClockPositionsInsteadOfDecoderPositions();
    seekForwardsAnchoredRuntimeClockAtTarget();
    coreEndOfFileEnqueuesRuntimeEofWithoutExternalEof();
    runtimeEndOfStreamPresentedEmitsExternalEof();
    runtimeEndOfStreamPresentedForwardsFinalRuntimeClockBeforeExternalEof();
    runtimeEndOfStreamPresentedWithoutCoreEofIsIgnored();
    beginSeekClearsQueuedCoreEofUntilNewRuntimeDrain();
    cancelFrameAcceptanceDropsPendingSeekCompletion();
    staleErrorIsIgnoredAfterTimelineIsAccepted();
}
