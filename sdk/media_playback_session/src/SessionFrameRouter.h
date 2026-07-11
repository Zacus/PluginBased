#pragma once

#include "SessionAudioConverter.h"
#include "SessionTimeline.h"

#include "media_sdk/DecodeFrameSink.h"
#include "media_sdk/MediaEvents.h"
#include "media_sdk/runtime/RuntimePlayer.h"
#include "media_sdk/runtime/RuntimeTypes.h"

#include <concepts>
#include <optional>
#include <utility>

namespace media_sdk::session {

template<typename RuntimeSink>
concept RuntimeFrameSink = requires(RuntimeSink& sink,
                                    runtime::RuntimeAudioFrame audio,
                                    runtime::RuntimeVideoFrame video) {
    { sink.enqueueAudio(std::move(audio)) } -> std::same_as<runtime::RuntimeFramePushResult>;
    { sink.enqueueVideo(std::move(video)) } -> std::same_as<runtime::RuntimeFramePushResult>;
};

DecodeFramePushResult mapRuntimeFramePushResult(runtime::RuntimeFramePushResult result);

template<RuntimeFrameSink RuntimeSink>
class BasicSessionFrameRouter final : public IDecodeFrameSink
{
public:
    BasicSessionFrameRouter(RuntimeSink& runtime, SessionTimeline& timeline)
        : m_runtime(runtime)
        , m_timeline(timeline)
    {
    }

    [[nodiscard("frame push result determines whether decode can continue, retry, or stop")]]
    DecodeFramePushResult pushAudio(AudioFrame frame, DecodeFrameMetadata metadata) override
    {
        const auto runtimeTimeline = runtimeTimelineFor(metadata);
        if (!runtimeTimeline.has_value())
            return { .status = DecodeFramePushStatus::StaleGeneration };

        auto converted = convertToRuntimeAudioFrame(frame);
        if (!converted.ok())
            return { .status = DecodeFramePushStatus::Closed };

        auto convertedFrame = std::move(converted.value());
        runtime::RuntimeAudioFrame runtimeFrame {
            .frame = std::move(convertedFrame.frame),
            .sessionId = runtimeTimeline->sessionId,
            .generation = runtimeTimeline->generation,
        };
        return mapRuntimeFramePushResult(m_runtime.enqueueAudio(std::move(runtimeFrame)));
    }

    [[nodiscard("frame push result determines whether decode can continue, retry, or stop")]]
    DecodeFramePushResult pushVideo(VideoFrame frame, DecodeFrameMetadata metadata) override
    {
        const auto runtimeTimeline = runtimeTimelineFor(metadata);
        if (!runtimeTimeline.has_value())
            return { .status = DecodeFramePushStatus::StaleGeneration };

        runtime::RuntimeVideoFrame runtimeFrame {
            .frame = std::move(frame),
            .sessionId = runtimeTimeline->sessionId,
            .generation = runtimeTimeline->generation,
            .videoPicturePool = metadata.videoPicturePool,
        };
        return mapRuntimeFramePushResult(m_runtime.enqueueVideo(std::move(runtimeFrame)));
    }

private:
    [[nodiscard("empty means decoded frame belongs to a stale core generation")]]
    std::optional<runtime::RuntimeTimeline> runtimeTimelineFor(DecodeFrameMetadata metadata) const
    {
        return m_timeline.runtimeForCoreEvent({
            .sessionId = metadata.sessionId,
            .generation = metadata.generation,
        });
    }

    RuntimeSink& m_runtime;
    SessionTimeline& m_timeline;
};

using SessionFrameRouter = BasicSessionFrameRouter<runtime::RuntimePlayer>;

} // namespace media_sdk::session
