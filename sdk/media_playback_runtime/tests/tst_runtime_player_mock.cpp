#include "media_sdk/runtime/RuntimePlayer.h"

#include <cassert>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace {

class MockAudioOutput final : public media_sdk::runtime::IAudioOutput {
public:
    media_sdk::Result<void> open(const media_sdk::runtime::AudioFormat& format) override
    {
        std::lock_guard lock(mutex);
        lastFormat = format;
        ++openCount;
        return media_sdk::Result<void>::success();
    }

    media_sdk::Result<void> write(media_sdk::runtime::AudioBufferView buffer) override
    {
        std::lock_guard lock(mutex);
        ++writeCount;
        writtenBytes += buffer.bytes.size();
        lastWritePts = buffer.pts;
        lastWriteGeneration = buffer.generation;
        return media_sdk::Result<void>::success();
    }

    media_sdk::runtime::ClockSnapshot clock() const override
    {
        std::lock_guard lock(mutex);
        return snapshot;
    }

    void pause() override { ++pauseCount; }
    void resume() override { ++resumeCount; }
    void flush() override
    {
        std::lock_guard lock(mutex);
        ++flushCount;
        ++snapshot.generation;
    }
    void close() override { ++closeCount; }

    void setClock(std::chrono::microseconds position, media_sdk::runtime::Generation generation)
    {
        std::lock_guard lock(mutex);
        snapshot.position = position;
        snapshot.generation = generation;
        snapshot.valid = true;
    }

    mutable std::mutex mutex;
    media_sdk::runtime::AudioFormat lastFormat {};
    media_sdk::runtime::ClockSnapshot snapshot {
        .position = 0us,
        .hardwareLatency = 0us,
        .queuedDuration = 0us,
        .generation = 1,
        .valid = true,
        .paused = false,
    };
    std::atomic_int openCount = 0;
    std::atomic_int writeCount = 0;
    std::atomic_int pauseCount = 0;
    std::atomic_int resumeCount = 0;
    std::atomic_int flushCount = 0;
    std::atomic_int closeCount = 0;
    std::size_t writtenBytes = 0;
    std::chrono::microseconds lastWritePts { 0 };
    media_sdk::runtime::Generation lastWriteGeneration = 0;
};

class MockPresenter final : public media_sdk::runtime::IVideoPresenter {
public:
    media_sdk::runtime::VideoPresenterCapabilities capabilities() const override
    {
        return {
            .supportsVideoToolboxPixelBuffer = true,
            .supportsCpuYuv = true,
            .asyncPresent = true,
            .maxPendingFrames = maxPendingFrames,
        };
    }

    void setEvents(media_sdk::runtime::IVideoPresenterEvents* nextEvents) override
    {
        std::lock_guard lock(mutex);
        events = nextEvents;
    }

    media_sdk::runtime::PresentResult present(
        media_sdk::VideoFrame frame,
        media_sdk::runtime::PresentTiming timing) override
    {
        std::lock_guard lock(mutex);
        ++presentCount;
        lastFrame = std::move(frame);
        lastTiming = timing;
        lastPresentId = ++nextPresentId;
        return {
            .id = lastPresentId,
            .status = nextStatus,
        };
    }

    void clear() override
    {
        ++clearCount;
    }

    void complete(
        media_sdk::runtime::PresentId id,
        media_sdk::runtime::PresentStatus status,
        media_sdk::runtime::PresentDiagnostics diagnostics = {})
    {
        media_sdk::runtime::IVideoPresenterEvents* target = nullptr;
        {
            std::lock_guard lock(mutex);
            target = events;
        }
        if (target) {
            target->onPresentComplete({
                .id = id,
                .status = status,
                .detail = {},
                .diagnostics = diagnostics,
            });
        }
    }

    media_sdk::runtime::PresentId lastId() const
    {
        std::lock_guard lock(mutex);
        return lastPresentId;
    }

    media_sdk::runtime::PresentTiming timing() const
    {
        std::lock_guard lock(mutex);
        return lastTiming;
    }

    mutable std::mutex mutex;
    media_sdk::runtime::IVideoPresenterEvents* events = nullptr;
    media_sdk::runtime::PresentStatus nextStatus = media_sdk::runtime::PresentStatus::Queued;
    media_sdk::VideoFrame lastFrame {};
    media_sdk::runtime::PresentTiming lastTiming {};
    media_sdk::runtime::PresentId nextPresentId = 0;
    media_sdk::runtime::PresentId lastPresentId = 0;
    std::atomic_int presentCount = 0;
    std::atomic_int clearCount = 0;
    std::uint32_t maxPendingFrames = 2;
};

class MockRuntimeEvents final : public media_sdk::runtime::IRuntimePlayerEvents {
public:
    void onFallbackToCpuRequested(
        media_sdk::runtime::RuntimeFallbackAction action) override
    {
        std::lock_guard lock(mutex);
        lastAction = action;
        ++fallbackRequestCount;
    }

    void onEndOfStreamPresented(media_sdk::runtime::RuntimeTimeline timeline) override
    {
        std::lock_guard lock(mutex);
        lastEofTimeline = timeline;
        ++eofPresentedCount;
    }

    mutable std::mutex mutex;
    media_sdk::runtime::RuntimeFallbackAction lastAction {};
    media_sdk::runtime::RuntimeTimeline lastEofTimeline {};
    std::atomic_int fallbackRequestCount = 0;
    std::atomic_int eofPresentedCount = 0;
};

bool waitUntil(const std::function<bool()>& predicate,
               std::chrono::milliseconds timeout = 500ms)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate())
            return true;
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}

media_sdk::AudioFrame makeAudioFrame(
    std::chrono::microseconds pts,
    std::vector<std::byte> samples = std::vector<std::byte>(128))
{
    return media_sdk::AudioFrame(media_sdk::AudioFrameDesc {
        .sampleFormat = media_sdk::AudioSampleFormat::Float32Interleaved,
        .sampleRate = 48000,
        .channels = 2,
        .pts = pts,
        .samples = samples,
    });
}

media_sdk::VideoFrame makeVideoFrame(
    std::chrono::microseconds pts,
    media_sdk::PixelFormat pixelFormat = media_sdk::PixelFormat::Yuv420P)
{
    media_sdk::NativeHandle nativeHandle;
    if (pixelFormat == media_sdk::PixelFormat::Native)
        nativeHandle.kind = media_sdk::NativeHandleKind::VideoToolboxPixelBuffer;

    return media_sdk::VideoFrame(media_sdk::VideoFrameDesc {
        .width = 1280,
        .height = 720,
        .pixelFormat = pixelFormat,
        .colorRange = media_sdk::ColorRange::Limited,
        .colorSpace = media_sdk::ColorSpace::Bt709,
        .pts = pts,
        .planes = {},
        .nativeHandle = nativeHandle,
    });
}

media_sdk::runtime::RuntimeAudioFrame runtimeAudio(
    media_sdk::runtime::SessionId sessionId,
    media_sdk::runtime::Generation generation,
    std::chrono::microseconds pts)
{
    return {
        .frame = makeAudioFrame(pts),
        .sessionId = sessionId,
        .generation = generation,
    };
}

media_sdk::runtime::RuntimeVideoFrame runtimeVideo(
    media_sdk::runtime::SessionId sessionId,
    media_sdk::runtime::Generation generation,
    std::chrono::microseconds pts,
    media_sdk::PixelFormat pixelFormat = media_sdk::PixelFormat::Yuv420P)
{
    return {
        .frame = makeVideoFrame(pts, pixelFormat),
        .sessionId = sessionId,
        .generation = generation,
    };
}

media_sdk::runtime::RuntimePlayer makePlayer(MockAudioOutput& audio, MockPresenter& presenter)
{
    return media_sdk::runtime::RuntimePlayer(
        {},
        {
            .audioOutput = &audio,
            .videoPresenter = &presenter,
        });
}

media_sdk::runtime::RuntimePlayer makePlayer(
    MockAudioOutput& audio,
    MockPresenter& presenter,
    media_sdk::runtime::RuntimePlayerConfig config)
{
    return media_sdk::runtime::RuntimePlayer(
        config,
        {
            .audioOutput = &audio,
            .videoPresenter = &presenter,
        });
}

media_sdk::runtime::RuntimePlayer makePlayer(
    MockAudioOutput& audio,
    MockPresenter& presenter,
    MockRuntimeEvents& events)
{
    return media_sdk::runtime::RuntimePlayer(
        {},
        {
            .audioOutput = &audio,
            .videoPresenter = &presenter,
            .events = &events,
        });
}

void openStartsNewSessionAndResetsQueues()
{
    MockAudioOutput audio;
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);

    assert(player.open().ok());
    assert(audio.openCount == 1);
    assert(audio.resumeCount == 1);
    assert(player.timeline().sessionId == 1);
    assert(player.timeline().generation == 1);
    player.enqueueAudio(runtimeAudio(0, 0, 0ms));
    std::this_thread::sleep_for(10ms);
    assert(audio.writeCount == 0);

    player.enqueueAudio(runtimeAudio(1, 1, 10ms));
    assert(waitUntil([&audio]() { return audio.writeCount == 1; }));

    assert(player.open().ok());
    assert(audio.resumeCount == 2);
    assert(player.timeline().sessionId == 2);
    assert(player.timeline().generation == 1);
    player.enqueueAudio(runtimeAudio(1, 1, 20ms));
    std::this_thread::sleep_for(10ms);
    assert(audio.writeCount == 1);
    player.enqueueAudio(runtimeAudio(2, 1, 20ms));
    assert(waitUntil([&audio]() { return audio.writeCount == 2; }));
}

void timelineTracksSeekGeneration()
{
    MockAudioOutput audio;
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    player.seek(500ms);
    const auto timeline = player.timeline();
    assert(timeline.sessionId == 1);
    assert(timeline.generation == 2);
}

void audioFramesAreWrittenToInjectedAudioOutput()
{
    MockAudioOutput audio;
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    player.enqueueAudio(runtimeAudio(1, 1, 42ms));
    assert(waitUntil([&audio]() { return audio.writeCount == 1; }));
    assert(audio.writtenBytes == 128);
    assert(audio.lastWritePts == 42ms);
    assert(audio.lastWriteGeneration == 1);
    assert(player.diagnostics().audioQueued == 1);
    assert(player.diagnostics().audioWritten == 1);
}

void videoFramesAreScheduledAgainstAudioClock()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    player.enqueueVideo(runtimeVideo(1, 1, 101ms));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));
    assert(presenter.timing().clock == 100ms);

    audio.setClock(250ms, 1);
    player.enqueueVideo(runtimeVideo(1, 1, 1ms));
    std::this_thread::sleep_for(10ms);
    assert(presenter.presentCount == 1);
    assert(player.diagnostics().videoDroppedLate == 1);
}

void videoWaitDecisionDelaysAndEventuallyPresentsSameFrame()
{
    MockAudioOutput audio;
    audio.setClock(0ms, 1);
    MockPresenter presenter;
    media_sdk::runtime::RuntimePlayerConfig config;
    config.syncConfig.submitLeadTime = 0us;
    config.syncConfig.maxScheduledWait = 5ms;
    auto player = makePlayer(audio, presenter, config);
    assert(player.open().ok());

    player.enqueueVideo(runtimeVideo(1, 1, 5ms));
    std::this_thread::sleep_for(2ms);
    assert(presenter.presentCount == 0);

    audio.setClock(5ms, 1);
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));
    assert(player.diagnostics().videoWaited >= 1);
    assert(player.diagnostics().videoPresented == 1);
}

void videoOnlyPlaybackUsesMonotonicClockWhenAudioClockIsDisabled()
{
    MockAudioOutput audio;
    audio.setClock(0ms, 1);
    MockPresenter presenter;
    media_sdk::runtime::RuntimePlayerConfig config;
    config.audioClockEnabled = false;
    config.syncConfig.submitLeadTime = 0us;
    config.syncConfig.maxScheduledWait = 2ms;
    auto player = makePlayer(audio, presenter, config);
    assert(player.open().ok());

    player.enqueueVideo(runtimeVideo(1, 1, 5ms));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }, 100ms));
}

void presenterBackpressureWaitsForCompletionBeforeSubmittingNextFrame()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    presenter.maxPendingFrames = 1;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    player.enqueueVideo(runtimeVideo(1, 1, 100ms));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));
    const auto firstPresentId = presenter.lastId();

    player.enqueueVideo(runtimeVideo(1, 1, 101ms));
    std::this_thread::sleep_for(20ms);
    assert(presenter.presentCount == 1);

    presenter.complete(firstPresentId, media_sdk::runtime::PresentStatus::Presented);
    assert(waitUntil([&presenter]() { return presenter.presentCount == 2; }));
}

void presenterBackpressureSeekCancelsWaitingOldGenerationFrame()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    presenter.maxPendingFrames = 1;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    player.enqueueVideo(runtimeVideo(1, 1, 100ms));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));
    const auto firstPresentId = presenter.lastId();

    player.enqueueVideo(runtimeVideo(1, 1, 101ms));
    std::this_thread::sleep_for(20ms);
    assert(presenter.presentCount == 1);

    player.seek(500ms);
    presenter.complete(firstPresentId, media_sdk::runtime::PresentStatus::Presented);
    std::this_thread::sleep_for(20ms);
    assert(presenter.presentCount == 1);

    audio.setClock(500ms, 2);
    player.enqueueVideo(runtimeVideo(1, 2, 500ms));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 2; }));
}

void eofCompletesOnlyAfterAudioAndVideoDrain()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    MockRuntimeEvents events;
    auto player = makePlayer(audio, presenter, events);
    assert(player.open().ok());

    player.enqueueAudio(runtimeAudio(1, 1, 10ms));
    player.enqueueVideo(runtimeVideo(1, 1, 100ms));
    assert(waitUntil([&audio]() { return audio.writeCount == 1; }));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));
    player.enqueueEndOfStream(1, 1);
    assert(waitUntil([&player]() { return player.diagnostics().eofPresented == 1; }));
    player.enqueueEndOfStream(1, 1);
    std::this_thread::sleep_for(10ms);

    const auto diagnostics = player.diagnostics();
    assert(diagnostics.audioWritten == 1);
    assert(diagnostics.videoPresented == 1);
    assert(diagnostics.eofAccepted == 2);
    assert(diagnostics.eofPresented == 1);
    assert(events.eofPresentedCount == 1);
    {
        std::lock_guard lock(events.mutex);
        assert(events.lastEofTimeline.sessionId == 1);
        assert(events.lastEofTimeline.generation == 1);
    }
}

void seekInvalidatesOldGenerationFramesAndCompletions()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    player.enqueueVideo(runtimeVideo(1, 1, 100ms, media_sdk::PixelFormat::Native));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));
    const auto oldPresentId = presenter.lastId();

    player.seek(500ms);
    presenter.complete(oldPresentId, media_sdk::runtime::PresentStatus::DeviceLost);
    assert(player.diagnostics().nativeFallbacks == 0);

    player.enqueueVideo(runtimeVideo(1, 1, 500ms));
    assert(presenter.presentCount == 1);

    audio.setClock(500ms, 2);
    player.enqueueVideo(runtimeVideo(1, 2, 500ms));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 2; }));
}

void stopAbortsQueuesPausesAudioClearsPresenterAndReturnsIdle()
{
    MockAudioOutput audio;
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    player.stop();
    assert(audio.pauseCount == 1);
    assert(audio.flushCount == 1);
    assert(audio.closeCount == 1);
    assert(presenter.clearCount == 1);
    assert(player.diagnostics().queueAbortCount == 2);

    player.enqueueAudio(runtimeAudio(1, 1, 10ms));
    assert(audio.writeCount == 0);
}

void nativePresenterFailureRunsFullFallbackTransition()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    player.enqueueVideo(runtimeVideo(1, 1, 100ms, media_sdk::PixelFormat::Native));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));

    presenter.complete(presenter.lastId(), media_sdk::runtime::PresentStatus::DeviceLost);
    assert(waitUntil([&player]() { return player.diagnostics().nativeFallbacks == 1; }));
    const auto diagnostics = player.diagnostics();
    assert(diagnostics.nativeFallbacks == 1);
    assert(diagnostics.queueAbortCount == 2);
    assert(audio.pauseCount == 1);
    assert(audio.flushCount == 1);
    assert(presenter.clearCount == 1);

    player.enqueueVideo(runtimeVideo(1, 1, 100ms));
    std::this_thread::sleep_for(10ms);
    assert(presenter.presentCount == 1);

    audio.setClock(100ms, 2);
    player.completeSeek(1, 2);
    player.enqueueVideo(runtimeVideo(1, 2, 100ms));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 2; }));
}

void nativeDiagnosticsTrackZeroCopySuccessWithoutCpuCopy()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    player.enqueueVideo(runtimeVideo(1, 1, 100ms, media_sdk::PixelFormat::Native));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));
    presenter.complete(
        presenter.lastId(),
        media_sdk::runtime::PresentStatus::Presented,
        {
            .nativeTextureCreated = 1,
            .nativeTextureDrawn = 1,
            .cpuTransferred = 0,
            .cpuMemcpy = 0,
        });

    assert(waitUntil([&player]() {
        return player.diagnostics().nativeTextureCreated == 1 &&
            player.diagnostics().nativeTextureDrawn == 1;
    }));

    const auto diagnostics = player.diagnostics();
    assert(diagnostics.nativeAccepted == 1);
    assert(diagnostics.nativePresented == 1);
    assert(diagnostics.nativeTextureCreated == 1);
    assert(diagnostics.nativeTextureDrawn == 1);
    assert(diagnostics.cpuCopied == 0);
    assert(diagnostics.cpuTransferred == 0);
    assert(diagnostics.cpuMemcpy == 0);
    assert(diagnostics.nativeFallbacks == 0);
}

void currentGenerationDeviceLostPausesAudioFlushesQueuesClearsPresenterAndRequestsCpuOnlyDecode()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    MockRuntimeEvents events;
    auto player = makePlayer(audio, presenter, events);
    assert(player.open().ok());

    player.enqueueVideo(runtimeVideo(1, 1, 100ms, media_sdk::PixelFormat::Native));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));
    presenter.complete(presenter.lastId(), media_sdk::runtime::PresentStatus::DeviceLost);

    assert(waitUntil([&player]() { return player.diagnostics().nativeFallbacks == 1; }));
    assert(waitUntil([&events]() { return events.fallbackRequestCount == 1; }));
    assert(audio.pauseCount == 1);
    assert(audio.flushCount == 1);
    assert(presenter.clearCount == 1);
    assert(player.diagnostics().queueAbortCount == 2);

    std::lock_guard lock(events.mutex);
    assert(events.lastAction.sessionId == 1);
    assert(events.lastAction.generation == 2);
    assert(events.lastAction.resumePosition == 100ms);
    assert(events.lastAction.outputPolicy == media_sdk::runtime::VideoOutputPolicy::CpuOnly);
    assert(!events.lastAction.preferNativeVideoFrames);
}

void oldGenerationDeviceLostDoesNotInterruptCurrentPlayback()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    MockRuntimeEvents events;
    auto player = makePlayer(audio, presenter, events);
    assert(player.open().ok());

    player.enqueueVideo(runtimeVideo(1, 1, 100ms, media_sdk::PixelFormat::Native));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));
    const auto oldPresentId = presenter.lastId();

    player.seek(500ms);
    presenter.complete(oldPresentId, media_sdk::runtime::PresentStatus::DeviceLost);
    std::this_thread::sleep_for(10ms);

    assert(player.diagnostics().nativeFallbacks == 0);
    assert(events.fallbackRequestCount == 0);
    assert(audio.pauseCount == 0);
    assert(presenter.clearCount == 1);
}

void fallbackSeekCompletionResumesAudioAndVideoScheduling()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    MockRuntimeEvents events;
    auto player = makePlayer(audio, presenter, events);
    assert(player.open().ok());

    player.enqueueVideo(runtimeVideo(1, 1, 100ms, media_sdk::PixelFormat::Native));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));
    presenter.complete(presenter.lastId(), media_sdk::runtime::PresentStatus::DeviceLost);
    assert(waitUntil([&events]() { return events.fallbackRequestCount == 1; }));

    audio.setClock(100ms, 2);
    player.enqueueVideo(runtimeVideo(1, 2, 100ms));
    std::this_thread::sleep_for(10ms);
    assert(presenter.presentCount == 1);

    player.completeSeek(1, 2);
    assert(audio.resumeCount == 2);
    player.enqueueVideo(runtimeVideo(1, 2, 100ms));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 2; }));

    const auto diagnostics = player.diagnostics();
    assert(diagnostics.nativeFallbacks == 1);
    assert(diagnostics.cpuPresented == 1);
}

} // namespace

int main()
{
    openStartsNewSessionAndResetsQueues();
    timelineTracksSeekGeneration();
    audioFramesAreWrittenToInjectedAudioOutput();
    videoFramesAreScheduledAgainstAudioClock();
    videoWaitDecisionDelaysAndEventuallyPresentsSameFrame();
    videoOnlyPlaybackUsesMonotonicClockWhenAudioClockIsDisabled();
    presenterBackpressureWaitsForCompletionBeforeSubmittingNextFrame();
    presenterBackpressureSeekCancelsWaitingOldGenerationFrame();
    eofCompletesOnlyAfterAudioAndVideoDrain();
    seekInvalidatesOldGenerationFramesAndCompletions();
    stopAbortsQueuesPausesAudioClearsPresenterAndReturnsIdle();
    nativePresenterFailureRunsFullFallbackTransition();
    nativeDiagnosticsTrackZeroCopySuccessWithoutCpuCopy();
    currentGenerationDeviceLostPausesAudioFlushesQueuesClearsPresenterAndRequestsCpuOnlyDecode();
    oldGenerationDeviceLostDoesNotInterruptCurrentPlayback();
    fallbackSeekCompletionResumesAudioAndVideoScheduling();
}
