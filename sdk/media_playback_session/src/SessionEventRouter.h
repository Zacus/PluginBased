#pragma once

#include "SessionTimeline.h"

#include "media_sdk/Player.h"
#include "media_sdk/Result.h"
#include "media_sdk/runtime/AudioOutput.h"
#include "media_sdk/runtime/RuntimeTypes.h"
#include "media_sdk/session/PlaybackSessionTypes.h"

#include <chrono>
#include <concepts>
#include <mutex>
#include <optional>
#include <variant>

namespace media_sdk::session {

template<typename RuntimeControl>
concept SessionRuntimeControl = requires(RuntimeControl& control,
                                         const MediaInfo& info,
                                         runtime::RuntimeTimeline timeline) {
    { control.openRuntimeForMedia(info) } -> std::same_as<Result<runtime::RuntimeTimeline>>;
    { control.completeSeek(timeline) } -> std::same_as<void>;
    { control.enqueueEndOfStream(timeline) } -> std::same_as<void>;
    { control.playbackClock(timeline) } -> std::same_as<std::optional<runtime::ClockSnapshot>>;
};

template<SessionRuntimeControl RuntimeControl>
class SessionEventRouter final : public IEventSink
{
public:
    SessionEventRouter(RuntimeControl& runtimeControl,
                       SessionTimeline& timeline,
                       ISessionEvents* events)
        : m_runtimeControl(runtimeControl)
        , m_timeline(timeline)
        , m_events(events)
    {
    }

    void beginSeek(runtime::RuntimeTimeline runtimeTimeline)
    {
        beginSeekInternal(runtimeTimeline, std::nullopt, false);
    }

    void beginSeek(runtime::RuntimeTimeline runtimeTimeline,
                   std::chrono::milliseconds targetPosition)
    {
        beginSeekInternal(runtimeTimeline, targetPosition, false);
    }

    void beginFallbackSeek(runtime::RuntimeTimeline runtimeTimeline)
    {
        beginSeekInternal(runtimeTimeline, std::nullopt, true);
    }

    void beginFallbackSeek(runtime::RuntimeTimeline runtimeTimeline,
                           std::chrono::milliseconds targetPosition)
    {
        beginSeekInternal(runtimeTimeline, targetPosition, true);
    }

    void cancelFrameAcceptance()
    {
        {
            std::lock_guard lock(m_mutex);
            m_pendingSeekTimeline.reset();
            m_pendingSeekTargetPosition.reset();
            m_positionGateTarget.reset();
            m_lastForwardedPosition.reset();
            m_runtimeEofForwarded = false;
            m_coreEofQueued = false;
            m_suppressMediaInfoUntilSeek = false;
        }
        m_timeline.clear();
    }

    void onEvent(const PlayerEvent& event) override
    {
        if (const auto* payload = std::get_if<MediaInfoEvent>(&event.payload)) {
            handleMediaInfo(event, *payload);
            return;
        }

        if (const auto* payload = std::get_if<SeekCompletedEvent>(&event.payload)) {
            handleSeekCompleted(event, *payload);
            return;
        }

        if (std::holds_alternative<EndOfFileEvent>(event.payload)) {
            handleCoreEndOfFile(event);
            return;
        }

        if (std::holds_alternative<PositionChangedEvent>(event.payload)) {
            handlePositionChanged(event);
            return;
        }

        if (std::holds_alternative<StateChangedEvent>(event.payload)) {
            forwardIfAccepted(event);
            return;
        }

        if (std::holds_alternative<ErrorEvent>(event.payload)) {
            forwardErrorIfCurrentOrUnopened(event);
        }
    }

    void onEndOfStreamPresented(runtime::RuntimeTimeline runtimeTimeline)
    {
        const auto coreTimeline = m_timeline.coreForRuntimeTimeline(runtimeTimeline);
        if (!coreTimeline.has_value())
            return;

        {
            std::lock_guard lock(m_mutex);
            if (!m_coreEofQueued)
                return;
            if (m_runtimeEofForwarded)
                return;
            m_runtimeEofForwarded = true;
        }

        forward({
            .metadata = *coreTimeline,
            .payload = EndOfFileEvent {},
        });
    }

private:
    void beginSeekInternal(runtime::RuntimeTimeline runtimeTimeline,
                           std::optional<std::chrono::milliseconds> targetPosition,
                           bool suppressMediaInfoUntilSeek)
    {
        {
            std::lock_guard lock(m_mutex);
            m_pendingSeekTimeline = runtimeTimeline;
            m_pendingSeekTargetPosition = targetPosition;
            m_positionGateTarget.reset();
            m_lastForwardedPosition.reset();
            m_runtimeEofForwarded = false;
            m_coreEofQueued = false;
            m_suppressMediaInfoUntilSeek = suppressMediaInfoUntilSeek;
        }
        m_timeline.clear();
    }

    void handleMediaInfo(const PlayerEvent& event, const MediaInfoEvent& payload)
    {
        {
            std::lock_guard lock(m_mutex);
            if (m_suppressMediaInfoUntilSeek)
                return;
            m_pendingSeekTimeline.reset();
            m_pendingSeekTargetPosition.reset();
            m_positionGateTarget.reset();
            m_lastForwardedPosition.reset();
            m_runtimeEofForwarded = false;
            m_coreEofQueued = false;
        }
        m_timeline.clear();

        const auto openResult = m_runtimeControl.openRuntimeForMedia(payload.info);
        if (!openResult.ok()) {
            forward({
                .metadata = event.metadata,
                .payload = ErrorEvent {
                    .error = openResult.error(),
                },
            });
            return;
        }

        m_timeline.acceptCoreTimeline(event.metadata, openResult.value());
        forward(event);
    }

    void handleSeekCompleted(const PlayerEvent& event, const SeekCompletedEvent& payload)
    {
        const auto seekState = takePendingSeekState(payload.position);
        if (!seekState.has_value())
            return;

        m_timeline.acceptCoreTimeline(event.metadata, seekState->timeline);
        m_runtimeControl.completeSeek(seekState->timeline);
        forward(event);
    }

    void handlePositionChanged(const PlayerEvent& event)
    {
        const auto runtimeTimeline = m_timeline.runtimeForCoreEvent(event.metadata);
        if (!runtimeTimeline.has_value())
            return;

        const auto clock = m_runtimeControl.playbackClock(*runtimeTimeline);
        if (!clock.has_value() || !clock->valid || clock->generation != runtimeTimeline->generation)
            return;

        const auto runtimePosition =
            std::chrono::duration_cast<std::chrono::milliseconds>(clock->position);

        {
            std::lock_guard lock(m_mutex);
            if (m_positionGateTarget.has_value()) {
                if (runtimePosition < *m_positionGateTarget)
                    return;
                m_positionGateTarget.reset();
            }
            if (m_lastForwardedPosition.has_value()
                && *m_lastForwardedPosition == runtimePosition) {
                return;
            }
            m_lastForwardedPosition = runtimePosition;
        }

        forward({
            .metadata = event.metadata,
            .payload = PositionChangedEvent {
                .position = runtimePosition,
            },
        });
    }

    void handleCoreEndOfFile(const PlayerEvent& event)
    {
        const auto runtimeTimeline = m_timeline.runtimeForCoreEvent(event.metadata);
        if (!runtimeTimeline.has_value())
            return;

        {
            std::lock_guard lock(m_mutex);
            if (m_coreEofQueued)
                return;
            m_coreEofQueued = true;
            m_runtimeEofForwarded = false;
        }
        m_runtimeControl.enqueueEndOfStream(*runtimeTimeline);
    }

    void forwardIfAccepted(const PlayerEvent& event)
    {
        if (m_timeline.acceptsCoreEvent(event.metadata))
            forward(event);
    }

    void forwardErrorIfCurrentOrUnopened(const PlayerEvent& event)
    {
        if (!m_timeline.hasAcceptedTimeline() || m_timeline.acceptsCoreEvent(event.metadata))
            forward(event);
    }

    void forward(const PlayerEvent& event)
    {
        if (m_events)
            m_events->onEvent(event);
    }

    struct PendingSeekState {
        runtime::RuntimeTimeline timeline {};
        std::chrono::milliseconds targetPosition { 0 };
    };

    [[nodiscard("empty means the seek completion does not match an active runtime seek")]]
    std::optional<PendingSeekState> takePendingSeekState(
        std::chrono::milliseconds completedPosition)
    {
        std::lock_guard lock(m_mutex);
        if (!m_pendingSeekTimeline.has_value())
            return std::nullopt;

        PendingSeekState state {
            .timeline = *m_pendingSeekTimeline,
            .targetPosition = m_pendingSeekTargetPosition.value_or(completedPosition),
        };
        m_pendingSeekTimeline.reset();
        m_pendingSeekTargetPosition.reset();
        m_positionGateTarget = state.targetPosition;
        m_lastForwardedPosition.reset();
        m_runtimeEofForwarded = false;
        m_coreEofQueued = false;
        m_suppressMediaInfoUntilSeek = false;
        return state;
    }

    RuntimeControl& m_runtimeControl;
    SessionTimeline& m_timeline;
    ISessionEvents* m_events = nullptr;
    std::mutex m_mutex;
    std::optional<runtime::RuntimeTimeline> m_pendingSeekTimeline;
    std::optional<std::chrono::milliseconds> m_pendingSeekTargetPosition;
    std::optional<std::chrono::milliseconds> m_positionGateTarget;
    std::optional<std::chrono::milliseconds> m_lastForwardedPosition;
    bool m_runtimeEofForwarded = false;
    bool m_coreEofQueued = false;
    bool m_suppressMediaInfoUntilSeek = false;
};

} // namespace media_sdk::session
