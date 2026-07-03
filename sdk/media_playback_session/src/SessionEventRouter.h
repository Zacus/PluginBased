#pragma once

#include "SessionTimeline.h"

#include "media_sdk/Player.h"
#include "media_sdk/Result.h"
#include "media_sdk/runtime/RuntimeTypes.h"
#include "media_sdk/session/PlaybackSessionTypes.h"

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
        beginSeek(runtimeTimeline, false);
    }

    void beginFallbackSeek(runtime::RuntimeTimeline runtimeTimeline)
    {
        beginSeek(runtimeTimeline, true);
    }

    void beginSeek(runtime::RuntimeTimeline runtimeTimeline, bool suppressMediaInfoUntilSeek)
    {
        {
            std::lock_guard lock(m_mutex);
            m_pendingSeekTimeline = runtimeTimeline;
            m_runtimeEofForwarded = false;
            m_coreEofQueued = false;
            m_suppressMediaInfoUntilSeek = suppressMediaInfoUntilSeek;
        }
        m_timeline.clear();
    }

    void cancelFrameAcceptance()
    {
        {
            std::lock_guard lock(m_mutex);
            m_pendingSeekTimeline.reset();
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

        if (std::holds_alternative<PositionChangedEvent>(event.payload)
            || std::holds_alternative<StateChangedEvent>(event.payload)) {
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
    void handleMediaInfo(const PlayerEvent& event, const MediaInfoEvent& payload)
    {
        {
            std::lock_guard lock(m_mutex);
            if (m_suppressMediaInfoUntilSeek)
                return;
            m_pendingSeekTimeline.reset();
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

    void handleSeekCompleted(const PlayerEvent& event, const SeekCompletedEvent&)
    {
        const auto runtimeTimeline = takePendingSeekTimeline();
        if (!runtimeTimeline.has_value())
            return;

        m_timeline.acceptCoreTimeline(event.metadata, *runtimeTimeline);
        m_runtimeControl.completeSeek(*runtimeTimeline);
        forward(event);
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

    [[nodiscard("empty means the seek completion does not match an active runtime seek")]]
    std::optional<runtime::RuntimeTimeline> takePendingSeekTimeline()
    {
        std::lock_guard lock(m_mutex);
        if (!m_pendingSeekTimeline.has_value())
            return std::nullopt;

        auto timeline = *m_pendingSeekTimeline;
        m_pendingSeekTimeline.reset();
        m_runtimeEofForwarded = false;
        m_coreEofQueued = false;
        m_suppressMediaInfoUntilSeek = false;
        return timeline;
    }

    RuntimeControl& m_runtimeControl;
    SessionTimeline& m_timeline;
    ISessionEvents* m_events = nullptr;
    std::mutex m_mutex;
    std::optional<runtime::RuntimeTimeline> m_pendingSeekTimeline;
    bool m_runtimeEofForwarded = false;
    bool m_coreEofQueued = false;
    bool m_suppressMediaInfoUntilSeek = false;
};

} // namespace media_sdk::session
