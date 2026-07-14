#include "PlaybackSessionTestHooks.h"

#include "media_sdk/session/PlaybackSession.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
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

media_sdk::MediaInfo mediaInfoWithAudio()
{
    return {
        .duration = 3000ms,
        .width = 3840,
        .height = 2160,
        .fps = 60.0,
        .sampleRate = 48000,
        .channels = 2,
        .formatName = "mov",
    };
}

media_sdk::PlayerEvent mediaInfoEvent(media_sdk::EventMetadata metadata)
{
    return {
        .metadata = metadata,
        .payload = media_sdk::MediaInfoEvent {
            .info = mediaInfoWithAudio(),
        },
    };
}

media_sdk::PlayerEvent seekCompletedEvent(media_sdk::EventMetadata metadata,
                                          std::chrono::milliseconds position,
                                          media_sdk::SeekRequestId requestId = 0)
{
    return {
        .metadata = metadata,
        .payload = media_sdk::SeekCompletedEvent {
            .position = position,
            .requestId = requestId,
        },
    };
}

media_sdk::VideoFrame makeVideoFrame()
{
    return media_sdk::VideoFrame(media_sdk::VideoFrameDesc {
        .width = 1920,
        .height = 1080,
        .pixelFormat = media_sdk::PixelFormat::Nv12,
        .pts = 40ms,
        .storage = std::make_shared<int>(1),
    });
}

media_sdk::Result<void> success()
{
    return media_sdk::Result<void>::success();
}

media_sdk::Result<void> seekFailure()
{
    return media_sdk::Result<void>::failure({
        .code = media_sdk::MediaErrorCode::SeekFailed,
        .message = "fallback seek failed",
    });
}

class DummyAudioOutput final : public media_sdk::runtime::IAudioOutput {
public:
    media_sdk::Result<void> open(const media_sdk::runtime::AudioFormat&) override
    {
        return success();
    }

    media_sdk::Result<void> write(media_sdk::runtime::AudioBufferView) override
    {
        return success();
    }

    media_sdk::runtime::ClockSnapshot clock() const override
    {
        return {};
    }

    void pause() override {}

    media_sdk::Result<void> resume() override
    {
        return success();
    }

    media_sdk::Result<void> flush() override
    {
        return media_sdk::Result<void>::success();
    }
    void close() override {}
};

class DummyVideoPresenter final : public media_sdk::runtime::IVideoPresenter {
public:
    media_sdk::runtime::VideoPresenterCapabilities capabilities() const override
    {
        return {};
    }

    void setEvents(media_sdk::runtime::IVideoPresenterEvents* events) override
    {
        this->events = events;
    }

    media_sdk::runtime::PresentResult present(media_sdk::VideoFrame,
                                              media_sdk::runtime::PresentTiming) override
    {
        return {
            .id = 1,
            .status = media_sdk::runtime::PresentStatus::Presented,
        };
    }

    void clear() override {}

    media_sdk::runtime::IVideoPresenterEvents* events = nullptr;
};

class RecordingSessionEvents final : public media_sdk::session::ISessionEvents {
public:
    void onEvent(const media_sdk::PlayerEvent& event) override
    {
        events.push_back(event);
    }

    void onNativeRenderingFailed() override
    {
        ++nativeRenderingFailedCount;
    }

    std::vector<media_sdk::PlayerEvent> events;
    int nativeRenderingFailedCount = 0;
};

struct TestContext;

class FakeCorePlayer final : public media_sdk::session::detail::IPlaybackSessionCorePlayer {
public:
    FakeCorePlayer(TestContext& context,
                   media_sdk::IEventSink& events,
                   media_sdk::IDecodeFrameSink& frames,
                   int index)
        : m_context(context)
        , m_events(events)
        , m_frames(frames)
        , m_index(index)
    {
    }

    media_sdk::Result<void> open(const std::filesystem::path& path) override;
    void play() override;
    void pause() override;
    void stop() override;
    media_sdk::Result<void> seek(std::chrono::milliseconds position,
                                  media_sdk::SeekPlaybackMode mode,
                                  media_sdk::SeekRequestId requestId) override;

    void emitMediaInfo(media_sdk::EventMetadata metadata)
    {
        m_events.onEvent(mediaInfoEvent(metadata));
    }

    void emitSeekCompleted(media_sdk::EventMetadata metadata, std::chrono::milliseconds position)
    {
        m_events.onEvent(seekCompletedEvent(metadata, position, lastSeekRequestId));
    }

    media_sdk::DecodeFramePushResult pushVideo(media_sdk::DecodeFrameMetadata metadata)
    {
        return m_frames.pushVideo(makeVideoFrame(), metadata);
    }

    int openCount = 0;
    int playCount = 0;
    int pauseCount = 0;
    int stopCount = 0;
    int seekCount = 0;
    std::filesystem::path lastOpenedPath;
    std::chrono::milliseconds lastSeekPosition { 0 };
    media_sdk::SeekRequestId lastSeekRequestId = 0;

private:
    TestContext& m_context;
    media_sdk::IEventSink& m_events;
    media_sdk::IDecodeFrameSink& m_frames;
    int m_index = 0;
};

class FakeRuntimePlayer final : public media_sdk::session::detail::IPlaybackSessionRuntimePlayer {
public:
    explicit FakeRuntimePlayer(TestContext& context)
    {
        (void)context;
    }

    media_sdk::Result<void> open() override;
    media_sdk::runtime::RuntimeFramePushResult enqueueAudio(
        media_sdk::runtime::RuntimeAudioFrame) override;
    media_sdk::runtime::RuntimeFramePushResult enqueueVideo(
        media_sdk::runtime::RuntimeVideoFrame frame) override;
    void enqueueEndOfStream(media_sdk::runtime::SessionId,
                            media_sdk::runtime::Generation) override {}
    void pause() override {}
    void resume() override {}
    void setAudioControls(media_sdk::runtime::RuntimeAudioControls) override {}
    void seek(std::chrono::microseconds) override {}
    media_sdk::Result<void> setPlaybackRate(
        double playbackRate,
        std::chrono::microseconds) override
    {
        currentPlaybackRate = playbackRate;
        ++currentTimeline.generation;
        return media_sdk::Result<void>::success();
    }
    double playbackRate() const override { return currentPlaybackRate; }
    void completeSeek(media_sdk::runtime::SessionId sessionId,
                      media_sdk::runtime::Generation generation) override;
    void notifyPresenterFailure(media_sdk::runtime::PresentStatus reason) override;
    void stop() override {}
    media_sdk::runtime::RuntimeDiagnostics diagnostics() const override;
    media_sdk::runtime::ClockSnapshot clock() const override;
    media_sdk::runtime::RuntimeTimeline timeline() const override;

    void triggerFallback(std::chrono::microseconds resumePosition)
    {
        events->onFallbackToCpuRequested({
            .sessionId = currentTimeline.sessionId,
            .generation = currentTimeline.generation,
            .resumePosition = resumePosition,
            .outputPolicy = media_sdk::runtime::VideoOutputPolicy::CpuOnly,
            .preferNativeVideoFrames = false,
        });
    }

    void triggerFallbackAfterRuntimeGenerationAdvance(std::chrono::microseconds resumePosition)
    {
        ++currentTimeline.generation;
        triggerFallback(resumePosition);
    }

    int openCount = 0;
    int videoPushCount = 0;
    int completeSeekCount = 0;
    int presenterFailureCount = 0;
    media_sdk::runtime::PresentStatus lastPresenterFailure =
        media_sdk::runtime::PresentStatus::Presented;
    media_sdk::runtime::RuntimeTimeline currentTimeline = runtimeTimeline(100, 1);
    media_sdk::runtime::RuntimeVideoFrame lastVideo {};
    media_sdk::runtime::IRuntimePlayerEvents* events = nullptr;
    std::chrono::microseconds clockPosition { 0 };
    double currentPlaybackRate = 1.0;
};

struct TestContext {
    std::vector<std::shared_ptr<FakeCorePlayer>> cores;
    std::vector<media_sdk::PlayerConfig> coreConfigs;
    std::shared_ptr<FakeRuntimePlayer> runtime;
    bool failNextCoreSeek = false;
};

media_sdk::Result<void> FakeCorePlayer::open(const std::filesystem::path& path)
{
    ++openCount;
    lastOpenedPath = path;
    return success();
}

void FakeCorePlayer::play()
{
    ++playCount;
}

void FakeCorePlayer::pause()
{
    ++pauseCount;
}

void FakeCorePlayer::stop()
{
    ++stopCount;
}

media_sdk::Result<void> FakeCorePlayer::seek(std::chrono::milliseconds position,
                                             media_sdk::SeekPlaybackMode,
                                             media_sdk::SeekRequestId requestId)
{
    ++seekCount;
    lastSeekPosition = position;
    lastSeekRequestId = requestId;
    if (m_context.failNextCoreSeek && m_index == 1)
        return seekFailure();
    return success();
}

media_sdk::Result<void> FakeRuntimePlayer::open()
{
    ++openCount;
    return success();
}

media_sdk::runtime::RuntimeFramePushResult FakeRuntimePlayer::enqueueAudio(
    media_sdk::runtime::RuntimeAudioFrame)
{
    return { .status = media_sdk::runtime::RuntimeFramePushStatus::Accepted };
}

media_sdk::runtime::RuntimeFramePushResult FakeRuntimePlayer::enqueueVideo(
    media_sdk::runtime::RuntimeVideoFrame frame)
{
    ++videoPushCount;
    lastVideo = std::move(frame);
    return { .status = media_sdk::runtime::RuntimeFramePushStatus::Accepted };
}

void FakeRuntimePlayer::completeSeek(media_sdk::runtime::SessionId,
                                     media_sdk::runtime::Generation)
{
    ++completeSeekCount;
}

void FakeRuntimePlayer::notifyPresenterFailure(media_sdk::runtime::PresentStatus reason)
{
    ++presenterFailureCount;
    lastPresenterFailure = reason;
    ++currentTimeline.generation;
    triggerFallback(clockPosition);
}

media_sdk::runtime::RuntimeDiagnostics FakeRuntimePlayer::diagnostics() const
{
    return {};
}

media_sdk::runtime::ClockSnapshot FakeRuntimePlayer::clock() const
{
    return {
        .position = clockPosition,
        .generation = currentTimeline.generation,
        .valid = true,
    };
}

media_sdk::runtime::RuntimeTimeline FakeRuntimePlayer::timeline() const
{
    return currentTimeline;
}

media_sdk::session::detail::PlaybackSessionFactories factoriesFor(TestContext& context)
{
    return {
        .createCore =
            [&context](media_sdk::PlayerConfig config,
                       media_sdk::IEventSink& events,
                       media_sdk::IDecodeFrameSink& frames) {
                context.coreConfigs.push_back(config);
                const auto index = static_cast<int>(context.cores.size());
                auto core = std::make_shared<FakeCorePlayer>(context, events, frames, index);
                context.cores.push_back(core);
                return core;
            },
        .createRuntime =
            [&context](media_sdk::runtime::RuntimePlayerConfig,
                       media_sdk::runtime::RuntimePlayerDependencies dependencies) {
                context.runtime = std::make_shared<FakeRuntimePlayer>(context);
                context.runtime->events = dependencies.events;
                return context.runtime;
            },
    };
}

std::unique_ptr<media_sdk::session::PlaybackSession> makeSession(
    TestContext& context,
    RecordingSessionEvents& events)
{
    static DummyAudioOutput audio;
    static DummyVideoPresenter presenter;

    return media_sdk::session::detail::PlaybackSessionTestHooks::create(
        {},
        {
            .audioOutput = &audio,
            .videoPresenter = &presenter,
            .events = &events,
        },
        factoriesFor(context));
}

void openNativeSession(TestContext& context,
                       media_sdk::session::PlaybackSession& session,
                       media_sdk::EventMetadata metadata)
{
    assert(session.open("sample.mov").ok());
    assert(context.cores.size() == 1);
    context.cores[0]->emitMediaInfo(metadata);
    assert(context.runtime);
    assert(context.coreConfigs[0].preferNativeVideoFrames);
    assert(context.coreConfigs[0].enableDecoderBufferPool);
}

void nativeFallbackReopensCoreWithCpuFramesAndSeeksToResumePosition()
{
    TestContext context;
    RecordingSessionEvents events;
    auto session = makeSession(context, events);
    openNativeSession(context, *session, coreTimeline(10, 3));

    context.runtime->triggerFallback(1500ms);

    assert(context.cores.size() == 2);
    assert(!context.coreConfigs[1].preferNativeVideoFrames);
    assert(context.coreConfigs[1].enableDecoderBufferPool);
    assert(context.cores[0]->stopCount == 1);
    assert(context.cores[1]->openCount == 1);
    assert(context.cores[1]->lastOpenedPath == std::filesystem::path("sample.mov"));
    assert(context.cores[1]->playCount == 1);
    assert(context.cores[1]->seekCount == 1);
    assert(context.cores[1]->lastSeekPosition == 1500ms);
    assert(events.nativeRenderingFailedCount == 1);

    context.cores[1]->emitMediaInfo(coreTimeline(11, 0));
    assert(context.runtime->openCount == 1);
    assert(events.events.size() == 1);

    const auto staleBeforeSeekComplete = context.cores[1]->pushVideo({
        .sessionId = 11,
        .generation = 1,
    });
    assert(staleBeforeSeekComplete.status == media_sdk::DecodeFramePushStatus::StaleGeneration);

    context.cores[1]->emitSeekCompleted(coreTimeline(11, 1), 1500ms);
    assert(context.runtime->completeSeekCount == 1);

    const auto accepted = context.cores[1]->pushVideo({
        .sessionId = 11,
        .generation = 1,
    });
    assert(accepted.status == media_sdk::DecodeFramePushStatus::Accepted);
    assert(context.runtime->videoPushCount == 1);
    assert(context.runtime->lastVideo.sessionId == 100);
    assert(context.runtime->lastVideo.generation == 1);
}

void externalNativeFailureUsesRuntimeFallbackState()
{
    TestContext context;
    RecordingSessionEvents events;
    auto session = makeSession(context, events);
    openNativeSession(context, *session, coreTimeline(10, 3));
    context.runtime->clockPosition = 2300ms;

    session->notifyNativeRenderingFailed();

    assert(context.runtime->presenterFailureCount == 1);
    assert(context.runtime->lastPresenterFailure
           == media_sdk::runtime::PresentStatus::UnsupportedNativeHandle);
    assert(context.cores.size() == 2);
    assert(!context.coreConfigs[1].preferNativeVideoFrames);
    assert(context.cores[0]->stopCount == 1);
    assert(context.cores[1]->openCount == 1);
    assert(context.cores[1]->seekCount == 1);
    assert(context.cores[1]->lastSeekPosition == 2300ms);
    assert(events.nativeRenderingFailedCount == 1);
}

void repeatedFallbackForSameGenerationIsIgnored()
{
    TestContext context;
    RecordingSessionEvents events;
    auto session = makeSession(context, events);
    openNativeSession(context, *session, coreTimeline(10, 3));

    context.runtime->triggerFallback(1500ms);
    context.runtime->triggerFallback(1600ms);

    assert(context.cores.size() == 2);
    assert(events.nativeRenderingFailedCount == 1);
}

void fallbackSeekFailureEmitsErrorEvent()
{
    TestContext context;
    context.failNextCoreSeek = true;
    RecordingSessionEvents events;
    auto session = makeSession(context, events);
    openNativeSession(context, *session, coreTimeline(10, 3));
    events.events.clear();

    context.runtime->triggerFallback(1500ms);

    assert(context.cores.size() == 2);
    assert(events.nativeRenderingFailedCount == 0);
    assert(events.events.size() == 1);
    const auto* error = std::get_if<media_sdk::ErrorEvent>(&events.events.back().payload);
    assert(error);
    assert(error->error.code == media_sdk::MediaErrorCode::SeekFailed);
}

void fallbackWhilePausedRestoresPausedCoreState()
{
    TestContext context;
    RecordingSessionEvents events;
    auto session = makeSession(context, events);
    openNativeSession(context, *session, coreTimeline(10, 3));

    session->play();
    session->pause();
    context.runtime->triggerFallback(1500ms);

    assert(context.cores.size() == 2);
    assert(context.cores[1]->playCount == 1);
    assert(context.cores[1]->pauseCount == 1);
    assert(events.nativeRenderingFailedCount == 1);
}

void fallbackWithNextRuntimeGenerationStillReopensCpuCore()
{
    TestContext context;
    RecordingSessionEvents events;
    auto session = makeSession(context, events);
    openNativeSession(context, *session, coreTimeline(10, 3));

    context.runtime->triggerFallbackAfterRuntimeGenerationAdvance(1500ms);

    assert(context.cores.size() == 2);
    assert(!context.coreConfigs[1].preferNativeVideoFrames);
    assert(context.cores[0]->stopCount == 1);
    assert(context.cores[1]->openCount == 1);
    assert(context.cores[1]->playCount == 1);
    assert(context.cores[1]->seekCount == 1);
    assert(context.cores[1]->lastSeekPosition == 1500ms);
    assert(events.nativeRenderingFailedCount == 1);

    context.cores[1]->emitSeekCompleted(coreTimeline(11, 1), 1500ms);
    assert(context.runtime->completeSeekCount == 1);

    const auto accepted = context.cores[1]->pushVideo({
        .sessionId = 11,
        .generation = 1,
    });
    assert(accepted.status == media_sdk::DecodeFramePushStatus::Accepted);
    assert(context.runtime->lastVideo.generation == 2);
}

} // namespace

int main()
{
    nativeFallbackReopensCoreWithCpuFramesAndSeeksToResumePosition();
    externalNativeFailureUsesRuntimeFallbackState();
    repeatedFallbackForSameGenerationIsIgnored();
    fallbackSeekFailureEmitsErrorEvent();
    fallbackWhilePausedRestoresPausedCoreState();
    fallbackWithNextRuntimeGenerationStillReopensCpuCore();
}
