#include "media_sdk/runtime/RuntimePlayer.h"

#include <cassert>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
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
        std::unique_lock lock(mutex);
        if (blockWrites) {
            writeBlocked = true;
            cv.notify_all();
            cv.wait(lock, [this]() { return !blockWrites; });
        }
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
    media_sdk::Result<void> resume() override
    {
        ++resumeCount;
        if (failResume) {
            return media_sdk::Result<void>::failure({
                .code = media_sdk::MediaErrorCode::InternalStateError,
                .message = "audio resume failed",
                .detail = {},
            });
        }
        return media_sdk::Result<void>::success();
    }
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

    void blockAudioWrites()
    {
        std::lock_guard lock(mutex);
        blockWrites = true;
        writeBlocked = false;
    }

    bool waitForBlockedWrite(std::chrono::milliseconds timeout = 500ms)
    {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, timeout, [this]() { return writeBlocked; });
    }

    void releaseAudioWrites()
    {
        {
            std::lock_guard lock(mutex);
            blockWrites = false;
        }
        cv.notify_all();
    }

    mutable std::mutex mutex;
    std::condition_variable cv;
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
    bool failResume = false;
    bool blockWrites = false;
    bool writeBlocked = false;
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
        lastPresentId = returnZeroPresentId ? 0 : ++nextPresentId;
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
    bool returnZeroPresentId = false;
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

void discardFramePushResult(media_sdk::runtime::RuntimeFramePushResult result)
{
    (void)result;
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
    const auto staleBeforeSession = player.enqueueAudio(runtimeAudio(0, 0, 0ms));
    assert(staleBeforeSession.status == media_sdk::runtime::RuntimeFramePushStatus::RejectedGeneration);
    std::this_thread::sleep_for(10ms);
    assert(audio.writeCount == 0);

    const auto firstAudio = player.enqueueAudio(runtimeAudio(1, 1, 10ms));
    assert(firstAudio.status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);
    assert(waitUntil([&audio]() { return audio.writeCount == 1; }));

    assert(player.open().ok());
    assert(audio.resumeCount == 2);
    assert(player.timeline().sessionId == 2);
    assert(player.timeline().generation == 1);
    const auto staleAfterReopen = player.enqueueAudio(runtimeAudio(1, 1, 20ms));
    assert(staleAfterReopen.status == media_sdk::runtime::RuntimeFramePushStatus::RejectedGeneration);
    std::this_thread::sleep_for(10ms);
    assert(audio.writeCount == 1);
    const auto secondAudio = player.enqueueAudio(runtimeAudio(2, 1, 20ms));
    assert(secondAudio.status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);
    assert(waitUntil([&audio]() { return audio.writeCount == 2; }));
}

void openFailsAndClosesAudioWhenResumeFails()
{
    MockAudioOutput audio;
    audio.failResume = true;
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);

    const auto openResult = player.open();

    assert(!openResult.ok());
    assert(audio.openCount == 1);
    assert(audio.resumeCount == 1);
    assert(audio.closeCount == 1);
    assert(player.timeline().sessionId == 0);
    assert(player.timeline().generation == 0);
    assert(presenter.events == nullptr);
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

    const auto audioResult = player.enqueueAudio(runtimeAudio(1, 1, 42ms));
    assert(audioResult.status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);
    assert(waitUntil([&audio]() { return audio.writeCount == 1; }));
    assert(audio.writtenBytes == 128);
    assert(audio.lastWritePts == 42ms);
    assert(audio.lastWriteGeneration == 1);
    assert(player.diagnostics().audioQueued == 1);
    assert(player.diagnostics().audioQueueHighWatermark >= 1);
    assert(player.diagnostics().audioBackpressureCount == 0);
    assert(player.diagnostics().audioWritten == 1);
}

void audioQueueBackpressureIsReportedInDiagnostics()
{
    MockAudioOutput audio;
    audio.blockAudioWrites();
    MockPresenter presenter;
    media_sdk::runtime::RuntimePlayerConfig config;
    config.audioQueueCapacity = 1;
    auto player = makePlayer(audio, presenter, config);
    assert(player.open().ok());

    const auto first = player.enqueueAudio(runtimeAudio(1, 1, 10ms));
    assert(first.status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);
    assert(audio.waitForBlockedWrite());

    const auto second = player.enqueueAudio(runtimeAudio(1, 1, 20ms));
    assert(second.status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);

    auto future = std::async(std::launch::async, [&player]() {
        return player.enqueueAudio(runtimeAudio(1, 1, 30ms));
    });
    std::this_thread::sleep_for(20ms);
    assert(future.wait_for(0ms) == std::future_status::timeout);

    audio.releaseAudioWrites();
    assert(future.wait_for(500ms) == std::future_status::ready);
    const auto third = future.get();
    assert(third.status == media_sdk::runtime::RuntimeFramePushStatus::Backpressured);
    assert(third.waitTime > 0us);
    assert(waitUntil([&audio]() { return audio.writeCount >= 3; }));

    const auto diagnostics = player.diagnostics();
    assert(diagnostics.audioBackpressureCount >= 1);
    assert(diagnostics.decodeFramePushWaitUs > 0);
    assert(diagnostics.audioQueueHighWatermark >= 1);
}

void videoFramesAreScheduledAgainstAudioClock()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    const auto staleVideoResult = player.enqueueVideo(runtimeVideo(1, 99, 101ms));
    assert(staleVideoResult.status == media_sdk::runtime::RuntimeFramePushStatus::RejectedGeneration);
    std::this_thread::sleep_for(10ms);
    assert(presenter.presentCount == 0);

    const auto videoResult = player.enqueueVideo(runtimeVideo(1, 1, 101ms));
    assert(videoResult.status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));
    assert(presenter.timing().clock == 100ms);

    audio.setClock(250ms, 1);
    const auto lateVideoResult = player.enqueueVideo(runtimeVideo(1, 1, 1ms));
    assert(lateVideoResult.status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);
    std::this_thread::sleep_for(10ms);
    assert(presenter.presentCount == 1);
    assert(player.diagnostics().videoQueued == 2);
    assert(player.diagnostics().videoQueueHighWatermark >= 1);
    assert(player.diagnostics().videoBackpressureCount == 0);
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

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 5ms)));
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

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 5ms)));
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

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 100ms)));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));
    const auto firstPresentId = presenter.lastId();

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 101ms)));
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

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 100ms)));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));
    const auto firstPresentId = presenter.lastId();

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 101ms)));
    std::this_thread::sleep_for(20ms);
    assert(presenter.presentCount == 1);

    player.seek(500ms);
    presenter.complete(firstPresentId, media_sdk::runtime::PresentStatus::Presented);
    std::this_thread::sleep_for(20ms);
    assert(presenter.presentCount == 1);

    audio.setClock(500ms, 2);
    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 2, 500ms)));
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

    discardFramePushResult(player.enqueueAudio(runtimeAudio(1, 1, 10ms)));
    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 100ms)));
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

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 100ms, media_sdk::PixelFormat::Native)));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));
    const auto oldPresentId = presenter.lastId();

    player.seek(500ms);
    presenter.complete(oldPresentId, media_sdk::runtime::PresentStatus::DeviceLost);
    assert(player.diagnostics().nativeFallbacks == 0);

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 500ms)));
    assert(presenter.presentCount == 1);

    audio.setClock(500ms, 2);
    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 2, 500ms)));
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

    discardFramePushResult(player.enqueueAudio(runtimeAudio(1, 1, 10ms)));
    assert(audio.writeCount == 0);
}

void nativePresenterFailureRunsFullFallbackTransition()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 100ms, media_sdk::PixelFormat::Native)));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));

    presenter.complete(presenter.lastId(), media_sdk::runtime::PresentStatus::DeviceLost);
    assert(waitUntil([&player]() { return player.diagnostics().nativeFallbacks == 1; }));
    const auto diagnostics = player.diagnostics();
    assert(diagnostics.nativeFallbacks == 1);
    assert(diagnostics.queueAbortCount == 2);
    assert(audio.pauseCount == 1);
    assert(audio.flushCount == 1);
    assert(presenter.clearCount == 1);

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 100ms)));
    std::this_thread::sleep_for(10ms);
    assert(presenter.presentCount == 1);

    audio.setClock(100ms, 2);
    player.completeSeek(1, 2);
    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 2, 100ms)));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 2; }));
}

void nativeDiagnosticsTrackZeroCopySuccessWithoutCpuCopy()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 100ms, media_sdk::PixelFormat::Native)));
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

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 100ms, media_sdk::PixelFormat::Native)));
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

void queuedPresenterResultWithZeroIdTriggersFallback()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    presenter.returnZeroPresentId = true;
    MockRuntimeEvents events;
    auto player = makePlayer(audio, presenter, events);
    assert(player.open().ok());

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 100ms, media_sdk::PixelFormat::Native)));

    assert(waitUntil([&player]() { return player.diagnostics().nativeFallbacks == 1; }));
    assert(waitUntil([&events]() { return events.fallbackRequestCount == 1; }));
    const auto diagnostics = player.diagnostics();
    assert(diagnostics.videoPresented == 0);
    assert(diagnostics.nativePresented == 0);
    assert(diagnostics.queueAbortCount == 2);
    assert(audio.pauseCount == 1);
    assert(audio.flushCount == 1);
    assert(presenter.clearCount == 1);
}

void oldGenerationDeviceLostDoesNotInterruptCurrentPlayback()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    MockRuntimeEvents events;
    auto player = makePlayer(audio, presenter, events);
    assert(player.open().ok());

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 100ms, media_sdk::PixelFormat::Native)));
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

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 100ms, media_sdk::PixelFormat::Native)));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));
    presenter.complete(presenter.lastId(), media_sdk::runtime::PresentStatus::DeviceLost);
    assert(waitUntil([&events]() { return events.fallbackRequestCount == 1; }));

    audio.setClock(100ms, 2);
    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 2, 100ms)));
    std::this_thread::sleep_for(10ms);
    assert(presenter.presentCount == 1);

    player.completeSeek(1, 2);
    assert(audio.resumeCount == 2);
    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 2, 100ms)));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 2; }));

    const auto diagnostics = player.diagnostics();
    assert(diagnostics.nativeFallbacks == 1);
    assert(diagnostics.cpuPresented == 1);
}

} // namespace

int main()
{
    openStartsNewSessionAndResetsQueues();
    openFailsAndClosesAudioWhenResumeFails();
    timelineTracksSeekGeneration();
    audioFramesAreWrittenToInjectedAudioOutput();
    audioQueueBackpressureIsReportedInDiagnostics();
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
    queuedPresenterResultWithZeroIdTriggersFallback();
    oldGenerationDeviceLostDoesNotInterruptCurrentPlayback();
    fallbackSeekCompletionResumesAudioAndVideoScheduling();
}
