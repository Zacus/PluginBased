#include "PlaybackSessionTestHooks.h"

#include "media_sdk/session/PlaybackSession.h"

#include <cassert>
#include <chrono>
#include <cstddef>
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
        .duration = 2000ms,
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
                                          std::chrono::milliseconds position)
{
    return {
        .metadata = metadata,
        .payload = media_sdk::SeekCompletedEvent {
            .position = position,
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

media_sdk::VideoFrame makeVideoFrame()
{
    return media_sdk::VideoFrame(media_sdk::VideoFrameDesc {
        .width = 1920,
        .height = 1080,
        .pixelFormat = media_sdk::PixelFormat::Nv12,
        .pts = 42ms,
        .storage = std::make_shared<int>(1),
    });
}

media_sdk::Result<void> success()
{
    return media_sdk::Result<void>::success();
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

    void flush() override {}
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

    void onRuntimeDiagnostics(media_sdk::runtime::RuntimeDiagnostics diagnostics) override
    {
        ++diagnosticsCount;
        lastDiagnostics = diagnostics;
    }

    std::vector<media_sdk::PlayerEvent> events;
    int diagnosticsCount = 0;
    media_sdk::runtime::RuntimeDiagnostics lastDiagnostics {};
};

struct TestContext;

class FakeCorePlayer final : public media_sdk::session::detail::IPlaybackSessionCorePlayer {
public:
    FakeCorePlayer(TestContext& context,
                   media_sdk::IEventSink& events,
                   media_sdk::IDecodeFrameSink& frames)
        : m_context(context)
        , m_events(events)
        , m_frames(frames)
    {
    }

    media_sdk::Result<void> open(const std::filesystem::path& path) override;
    void play() override;
    void pause() override;
    void stop() override;
    media_sdk::Result<void> seek(std::chrono::milliseconds position) override;

    void emitMediaInfo(media_sdk::EventMetadata metadata)
    {
        m_events.onEvent(mediaInfoEvent(metadata));
    }

    void emitSeekCompleted(media_sdk::EventMetadata metadata, std::chrono::milliseconds position)
    {
        m_events.onEvent(seekCompletedEvent(metadata, position));
    }

    void emitEndOfFile(media_sdk::EventMetadata metadata)
    {
        m_events.onEvent(eofEvent(metadata));
    }

    media_sdk::DecodeFramePushResult pushVideo(media_sdk::VideoFrame frame,
                                               media_sdk::DecodeFrameMetadata metadata)
    {
        return m_frames.pushVideo(std::move(frame), metadata);
    }

    int openCount = 0;
    int playCount = 0;
    int pauseCount = 0;
    int stopCount = 0;
    int seekCount = 0;
    std::filesystem::path lastOpenedPath;
    std::chrono::milliseconds lastSeekPosition { 0 };

private:
    TestContext& m_context;
    media_sdk::IEventSink& m_events;
    media_sdk::IDecodeFrameSink& m_frames;
};

class FakeRuntimePlayer final : public media_sdk::session::detail::IPlaybackSessionRuntimePlayer {
public:
    explicit FakeRuntimePlayer(TestContext& context)
        : m_context(context)
    {
    }

    media_sdk::Result<void> open() override;
    media_sdk::runtime::RuntimeFramePushResult enqueueAudio(
        media_sdk::runtime::RuntimeAudioFrame frame) override;
    media_sdk::runtime::RuntimeFramePushResult enqueueVideo(
        media_sdk::runtime::RuntimeVideoFrame frame) override;
    void enqueueEndOfStream(media_sdk::runtime::SessionId sessionId,
                            media_sdk::runtime::Generation generation) override;
    void pause() override;
    void resume() override;
    void seek(std::chrono::microseconds position) override;
    void completeSeek(media_sdk::runtime::SessionId sessionId,
                      media_sdk::runtime::Generation generation) override;
    void stop() override;
    media_sdk::runtime::RuntimeDiagnostics diagnostics() const override;
    media_sdk::runtime::RuntimeTimeline timeline() const override;

    void triggerEndOfStreamPresented()
    {
        m_events->onEndOfStreamPresented(currentTimeline);
    }

    int openCount = 0;
    int audioPushCount = 0;
    int videoPushCount = 0;
    int eofCount = 0;
    int pauseCount = 0;
    int resumeCount = 0;
    int seekCount = 0;
    int completeSeekCount = 0;
    int stopCount = 0;
    bool paused = false;
    media_sdk::runtime::RuntimeVideoFrame lastVideo {};
    media_sdk::runtime::RuntimeTimeline currentTimeline = runtimeTimeline(100, 1);
    std::chrono::microseconds lastSeekPosition { 0 };
    media_sdk::runtime::IRuntimePlayerEvents* m_events = nullptr;

private:
    TestContext& m_context;
};

struct TestContext {
    std::vector<std::string> operations;
    std::shared_ptr<FakeCorePlayer> core;
    std::shared_ptr<FakeRuntimePlayer> runtime;
    media_sdk::runtime::RuntimeDiagnostics diagnostics {};
};

media_sdk::Result<void> FakeCorePlayer::open(const std::filesystem::path& path)
{
    ++openCount;
    lastOpenedPath = path;
    m_context.operations.push_back("core.open");
    return success();
}

void FakeCorePlayer::play()
{
    ++playCount;
    m_context.operations.push_back("core.play");
}

void FakeCorePlayer::pause()
{
    ++pauseCount;
    m_context.operations.push_back("core.pause");
}

void FakeCorePlayer::stop()
{
    ++stopCount;
    m_context.operations.push_back("core.stop");
}

media_sdk::Result<void> FakeCorePlayer::seek(std::chrono::milliseconds position)
{
    ++seekCount;
    lastSeekPosition = position;
    m_context.operations.push_back("core.seek");
    return success();
}

media_sdk::Result<void> FakeRuntimePlayer::open()
{
    ++openCount;
    m_context.operations.push_back("runtime.open");
    return success();
}

media_sdk::runtime::RuntimeFramePushResult FakeRuntimePlayer::enqueueAudio(
    media_sdk::runtime::RuntimeAudioFrame)
{
    ++audioPushCount;
    return { .status = media_sdk::runtime::RuntimeFramePushStatus::Accepted };
}

media_sdk::runtime::RuntimeFramePushResult FakeRuntimePlayer::enqueueVideo(
    media_sdk::runtime::RuntimeVideoFrame frame)
{
    ++videoPushCount;
    lastVideo = std::move(frame);
    return { .status = media_sdk::runtime::RuntimeFramePushStatus::Accepted };
}

void FakeRuntimePlayer::enqueueEndOfStream(media_sdk::runtime::SessionId,
                                           media_sdk::runtime::Generation)
{
    ++eofCount;
}

void FakeRuntimePlayer::pause()
{
    ++pauseCount;
    paused = true;
    m_context.operations.push_back("runtime.pause");
}

void FakeRuntimePlayer::resume()
{
    ++resumeCount;
    paused = false;
    m_context.operations.push_back("runtime.resume");
}

void FakeRuntimePlayer::seek(std::chrono::microseconds position)
{
    ++seekCount;
    lastSeekPosition = position;
    ++currentTimeline.generation;
    m_context.operations.push_back("runtime.seek");
}

void FakeRuntimePlayer::completeSeek(media_sdk::runtime::SessionId,
                                     media_sdk::runtime::Generation)
{
    ++completeSeekCount;
    m_context.operations.push_back("runtime.completeSeek");
}

void FakeRuntimePlayer::stop()
{
    ++stopCount;
    m_context.operations.push_back("runtime.stop");
}

media_sdk::runtime::RuntimeDiagnostics FakeRuntimePlayer::diagnostics() const
{
    return m_context.diagnostics;
}

media_sdk::runtime::RuntimeTimeline FakeRuntimePlayer::timeline() const
{
    return currentTimeline;
}

media_sdk::session::detail::PlaybackSessionFactories factoriesFor(TestContext& context)
{
    return {
        .createCore =
            [&context](media_sdk::PlayerConfig,
                       media_sdk::IEventSink& events,
                       media_sdk::IDecodeFrameSink& frames) {
                context.core = std::make_shared<FakeCorePlayer>(context, events, frames);
                return context.core;
            },
        .createRuntime =
            [&context](media_sdk::runtime::RuntimePlayerConfig,
                       media_sdk::runtime::RuntimePlayerDependencies dependencies) {
                context.runtime = std::make_shared<FakeRuntimePlayer>(context);
                context.runtime->m_events = dependencies.events;
                return context.runtime;
            },
    };
}

std::unique_ptr<media_sdk::session::PlaybackSession> makeSession(
    TestContext& context,
    media_sdk::runtime::IAudioOutput* audioOutput,
    media_sdk::runtime::IVideoPresenter* videoPresenter,
    media_sdk::session::ISessionEvents* events = nullptr)
{
    return media_sdk::session::detail::PlaybackSessionTestHooks::create(
        {},
        {
            .audioOutput = audioOutput,
            .videoPresenter = videoPresenter,
            .events = events,
        },
        factoriesFor(context));
}

void missingAudioOutputReturnsOpenFailure()
{
    TestContext context;
    DummyVideoPresenter presenter;
    RecordingSessionEvents events;
    auto session = makeSession(context, nullptr, &presenter, &events);

    const auto result = session->open("sample.mov");

    assert(!result.ok());
    assert(result.error().code == media_sdk::MediaErrorCode::InternalStateError);
    assert(!context.core);
    assert(!context.runtime);
    assert(events.events.empty());
}

void missingVideoPresenterReturnsOpenFailure()
{
    TestContext context;
    DummyAudioOutput audio;
    RecordingSessionEvents events;
    auto session = makeSession(context, &audio, nullptr, &events);

    const auto result = session->open("sample.mov");

    assert(!result.ok());
    assert(result.error().code == media_sdk::MediaErrorCode::InternalStateError);
    assert(!context.core);
    assert(!context.runtime);
    assert(events.events.empty());
}

void openCreatesCoreAndWaitsForMediaInfoBeforeFrameAcceptance()
{
    TestContext context;
    DummyAudioOutput audio;
    DummyVideoPresenter presenter;
    RecordingSessionEvents events;
    auto session = makeSession(context, &audio, &presenter, &events);

    const auto result = session->open("sample.mov");

    assert(result.ok());
    assert(context.core);
    assert(context.core->openCount == 1);
    assert(context.core->lastOpenedPath == std::filesystem::path("sample.mov"));
    assert(!context.runtime);

    const auto stalePush = context.core->pushVideo(makeVideoFrame(), {
        .sessionId = 10,
        .generation = 3,
    });
    assert(stalePush.status == media_sdk::DecodeFramePushStatus::StaleGeneration);

    context.core->emitMediaInfo(coreTimeline(10, 3));

    assert(context.runtime);
    assert(context.runtime->openCount == 1);
    assert(events.events.size() == 1);
    assert(std::holds_alternative<media_sdk::MediaInfoEvent>(events.events.back().payload));

    const auto acceptedPush = context.core->pushVideo(makeVideoFrame(), {
        .sessionId = 10,
        .generation = 3,
    });
    assert(acceptedPush.status == media_sdk::DecodeFramePushStatus::Accepted);
    assert(context.runtime->videoPushCount == 1);
    assert(context.runtime->lastVideo.sessionId == 100);
    assert(context.runtime->lastVideo.generation == 1);
}

void playCallsCorePlayAndRuntimeResume()
{
    TestContext context;
    DummyAudioOutput audio;
    DummyVideoPresenter presenter;
    auto session = makeSession(context, &audio, &presenter);
    assert(session->open("sample.mov").ok());
    context.core->emitMediaInfo(coreTimeline(10, 3));
    context.operations.clear();

    session->play();

    assert(context.runtime->resumeCount == 1);
    assert(context.core->playCount == 1);
    assert((context.operations == std::vector<std::string> { "runtime.resume", "core.play" }));
}

void pauseCallsCorePauseAndRuntimePause()
{
    TestContext context;
    DummyAudioOutput audio;
    DummyVideoPresenter presenter;
    auto session = makeSession(context, &audio, &presenter);
    assert(session->open("sample.mov").ok());
    context.core->emitMediaInfo(coreTimeline(10, 3));
    context.operations.clear();

    session->pause();

    assert(context.runtime->pauseCount == 1);
    assert(context.core->pauseCount == 1);
    assert((context.operations == std::vector<std::string> { "runtime.pause", "core.pause" }));
}

void seekCallsRuntimeSeekBeforeCoreSeek()
{
    TestContext context;
    DummyAudioOutput audio;
    DummyVideoPresenter presenter;
    RecordingSessionEvents events;
    auto session = makeSession(context, &audio, &presenter, &events);
    assert(session->open("sample.mov").ok());
    context.core->emitMediaInfo(coreTimeline(10, 3));
    context.operations.clear();

    const auto result = session->seek(750ms);

    assert(result.ok());
    assert(context.runtime->seekCount == 1);
    assert(context.core->seekCount == 1);
    assert((context.operations == std::vector<std::string> { "runtime.seek", "core.seek" }));
    assert(context.runtime->lastSeekPosition == 750ms);
    assert(context.core->lastSeekPosition == 750ms);

    context.core->emitSeekCompleted(coreTimeline(10, 4), 750ms);
    assert(context.runtime->completeSeekCount == 1);
    assert(events.events.size() == 2);
    assert(std::holds_alternative<media_sdk::SeekCompletedEvent>(events.events.back().payload));
}

void pauseBeforeMediaInfoPausesRuntimeAfterItOpens()
{
    TestContext context;
    DummyAudioOutput audio;
    DummyVideoPresenter presenter;
    RecordingSessionEvents events;
    auto session = makeSession(context, &audio, &presenter, &events);
    assert(session->open("sample.mov").ok());

    session->pause();
    assert(context.core->pauseCount == 1);
    context.operations.clear();

    context.core->emitMediaInfo(coreTimeline(10, 3));

    assert(context.runtime);
    assert(context.runtime->pauseCount == 1);
    assert(context.runtime->resumeCount == 0);
    assert(context.runtime->paused);
}

void playBeforeMediaInfoResumesRuntimeAfterItOpens()
{
    TestContext context;
    DummyAudioOutput audio;
    DummyVideoPresenter presenter;
    RecordingSessionEvents events;
    auto session = makeSession(context, &audio, &presenter, &events);
    assert(session->open("sample.mov").ok());

    session->play();
    assert(context.core->playCount == 1);
    context.operations.clear();

    context.core->emitMediaInfo(coreTimeline(10, 3));

    assert(context.runtime);
    assert(context.runtime->resumeCount == 1);
    assert(context.runtime->pauseCount == 0);
    assert(!context.runtime->paused);
}

void seekWhilePlayingCancelsOldFramePushUntilSeekCompletes()
{
    TestContext context;
    DummyAudioOutput audio;
    DummyVideoPresenter presenter;
    RecordingSessionEvents events;
    auto session = makeSession(context, &audio, &presenter, &events);
    assert(session->open("sample.mov").ok());
    context.core->emitMediaInfo(coreTimeline(10, 3));

    const auto acceptedBeforeSeek = context.core->pushVideo(makeVideoFrame(), {
        .sessionId = 10,
        .generation = 3,
    });
    assert(acceptedBeforeSeek.status == media_sdk::DecodeFramePushStatus::Accepted);

    assert(session->seek(900ms).ok());

    const auto staleOldGeneration = context.core->pushVideo(makeVideoFrame(), {
        .sessionId = 10,
        .generation = 3,
    });
    assert(staleOldGeneration.status == media_sdk::DecodeFramePushStatus::StaleGeneration);

    const auto staleBeforeCompletion = context.core->pushVideo(makeVideoFrame(), {
        .sessionId = 10,
        .generation = 4,
    });
    assert(staleBeforeCompletion.status == media_sdk::DecodeFramePushStatus::StaleGeneration);

    context.core->emitSeekCompleted(coreTimeline(10, 4), 900ms);

    const auto acceptedAfterCompletion = context.core->pushVideo(makeVideoFrame(), {
        .sessionId = 10,
        .generation = 4,
    });
    assert(acceptedAfterCompletion.status == media_sdk::DecodeFramePushStatus::Accepted);
}

void seekWhilePausedDoesNotResumeAfterSeekCompletion()
{
    TestContext context;
    DummyAudioOutput audio;
    DummyVideoPresenter presenter;
    RecordingSessionEvents events;
    auto session = makeSession(context, &audio, &presenter, &events);
    assert(session->open("sample.mov").ok());
    context.core->emitMediaInfo(coreTimeline(10, 3));
    session->pause();
    context.operations.clear();

    assert(session->seek(1200ms).ok());
    context.core->emitSeekCompleted(coreTimeline(10, 4), 1200ms);

    for (const auto& operation : context.operations) {
        assert(operation != "runtime.resume");
        assert(operation != "core.play");
    }
    assert(context.runtime->paused);
}

void stopDuringPendingSeekSuppressesStaleSeekCompletionAndFrames()
{
    TestContext context;
    DummyAudioOutput audio;
    DummyVideoPresenter presenter;
    RecordingSessionEvents events;
    auto session = makeSession(context, &audio, &presenter, &events);
    assert(session->open("sample.mov").ok());
    context.core->emitMediaInfo(coreTimeline(10, 3));
    events.events.clear();

    assert(session->seek(1500ms).ok());
    session->stop();

    context.core->emitSeekCompleted(coreTimeline(10, 4), 1500ms);
    assert(events.events.empty());

    const auto stalePush = context.core->pushVideo(makeVideoFrame(), {
        .sessionId = 10,
        .generation = 4,
    });
    assert(stalePush.status == media_sdk::DecodeFramePushStatus::StaleGeneration);
}

void openAnotherFileRejectsPreviousFileFramesAndEof()
{
    TestContext context;
    DummyAudioOutput audio;
    DummyVideoPresenter presenter;
    RecordingSessionEvents events;
    auto session = makeSession(context, &audio, &presenter, &events);
    assert(session->open("first.mov").ok());
    auto firstCore = context.core;
    context.core->emitMediaInfo(coreTimeline(10, 3));
    events.events.clear();

    assert(session->open("second.mov").ok());
    auto secondCore = context.core;
    assert(secondCore != firstCore);
    secondCore->emitMediaInfo(coreTimeline(11, 1));
    events.events.clear();

    const auto stalePush = firstCore->pushVideo(makeVideoFrame(), {
        .sessionId = 10,
        .generation = 3,
    });
    assert(stalePush.status == media_sdk::DecodeFramePushStatus::StaleGeneration);

    firstCore->emitEndOfFile(coreTimeline(10, 3));
    context.runtime->triggerEndOfStreamPresented();
    assert(events.events.empty());

    const auto acceptedPush = secondCore->pushVideo(makeVideoFrame(), {
        .sessionId = 11,
        .generation = 1,
    });
    assert(acceptedPush.status == media_sdk::DecodeFramePushStatus::Accepted);
}

void coreEofWaitsForRuntimeEndOfStreamBeforeExternalEof()
{
    TestContext context;
    DummyAudioOutput audio;
    DummyVideoPresenter presenter;
    RecordingSessionEvents events;
    auto session = makeSession(context, &audio, &presenter, &events);
    assert(session->open("sample.mov").ok());
    context.core->emitMediaInfo(coreTimeline(10, 3));
    events.events.clear();

    context.core->emitEndOfFile(coreTimeline(10, 3));
    assert(events.events.empty());

    context.runtime->triggerEndOfStreamPresented();
    assert(events.events.size() == 1);
    assert(std::holds_alternative<media_sdk::EndOfFileEvent>(events.events.back().payload));
}

void stopStopsCoreAndRuntimeExactlyOnce()
{
    TestContext context;
    DummyAudioOutput audio;
    DummyVideoPresenter presenter;
    auto session = makeSession(context, &audio, &presenter);
    assert(session->open("sample.mov").ok());
    context.core->emitMediaInfo(coreTimeline(10, 3));

    session->stop();

    assert(context.core->stopCount == 1);
    assert(context.runtime->stopCount == 1);

    session.reset();
    assert(context.core->stopCount == 1);
    assert(context.runtime->stopCount == 1);
}

void diagnosticsAndTimelineForwardRuntimeValues()
{
    TestContext context;
    DummyAudioOutput audio;
    DummyVideoPresenter presenter;
    auto session = makeSession(context, &audio, &presenter);
    assert(session->open("sample.mov").ok());
    context.core->emitMediaInfo(coreTimeline(10, 3));
    context.diagnostics.videoPresented = 42;

    assert(session->timeline().sessionId == 100);
    assert(session->timeline().generation == 1);
    assert(session->diagnostics().videoPresented == 42);
}

void diagnosticsExposePerformanceGuardrailsAndForwardSnapshots()
{
    TestContext context;
    DummyAudioOutput audio;
    DummyVideoPresenter presenter;
    RecordingSessionEvents events;
    auto session = makeSession(context, &audio, &presenter, &events);
    assert(session->open("sample.mov").ok());

    context.diagnostics.nativeFallbacks = 2;
    context.diagnostics.decodeFramePushWaitUs = 12'345;
    context.diagnostics.audioBackpressureCount = 3;
    context.diagnostics.videoBackpressureCount = 4;
    context.diagnostics.nativePresented = 5;
    context.diagnostics.cpuPresented = 6;
    context.diagnostics.videoPresented = 11;
    context.core->emitMediaInfo(coreTimeline(10, 3));

    assert(events.diagnosticsCount == 1);
    assert(events.lastDiagnostics.nativeFallbacks == 2);
    assert(events.lastDiagnostics.decodeFramePushWaitUs == 12'345);
    assert(events.lastDiagnostics.audioBackpressureCount == 3);
    assert(events.lastDiagnostics.videoBackpressureCount == 4);
    assert(events.lastDiagnostics.nativePresented == 5);
    assert(events.lastDiagnostics.cpuPresented == 6);
    assert(events.lastDiagnostics.videoPresented == 11);

    const auto diagnostics = session->diagnostics();
    assert(diagnostics.nativeFallbacks == 2);
    assert(diagnostics.decodeFramePushWaitUs == 12'345);
    assert(diagnostics.nativePresented == 5);
    assert(diagnostics.cpuPresented == 6);

    context.diagnostics.eofPresented = 1;
    context.core->emitEndOfFile(coreTimeline(10, 3));
    context.runtime->triggerEndOfStreamPresented();

    assert(events.diagnosticsCount == 3);
    assert(events.lastDiagnostics.eofPresented == 1);
}

} // namespace

int main()
{
    missingAudioOutputReturnsOpenFailure();
    missingVideoPresenterReturnsOpenFailure();
    openCreatesCoreAndWaitsForMediaInfoBeforeFrameAcceptance();
    playCallsCorePlayAndRuntimeResume();
    pauseCallsCorePauseAndRuntimePause();
    seekCallsRuntimeSeekBeforeCoreSeek();
    pauseBeforeMediaInfoPausesRuntimeAfterItOpens();
    playBeforeMediaInfoResumesRuntimeAfterItOpens();
    seekWhilePlayingCancelsOldFramePushUntilSeekCompletes();
    seekWhilePausedDoesNotResumeAfterSeekCompletion();
    stopDuringPendingSeekSuppressesStaleSeekCompletionAndFrames();
    openAnotherFileRejectsPreviousFileFramesAndEof();
    coreEofWaitsForRuntimeEndOfStreamBeforeExternalEof();
    stopStopsCoreAndRuntimeExactlyOnce();
    diagnosticsAndTimelineForwardRuntimeValues();
    diagnosticsExposePerformanceGuardrailsAndForwardSnapshots();
}
