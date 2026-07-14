#include "media_sdk/runtime/RuntimePlayer.h"

#include <cassert>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
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
        lastWrittenBytes.assign(buffer.bytes.begin(), buffer.bytes.end());
        lastWritePts = buffer.pts;
        lastWriteGeneration = buffer.generation;
        lastWritePlaybackRate = buffer.playbackRate;
        writePts.push_back(buffer.pts);
        if (clockFromWrites) {
            if (firstWriteAfterFlush || !snapshot.valid || snapshot.generation != buffer.generation) {
                snapshot.position = buffer.pts;
                firstWriteAfterFlush = false;
            }
            snapshot.generation = buffer.generation;
            snapshot.valid = true;
        }
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
    media_sdk::Result<void> flush() override
    {
        {
            std::lock_guard lock(mutex);
            ++flushCount;
            if (failFlush) {
                return media_sdk::Result<void>::failure({
                    .code = media_sdk::MediaErrorCode::InternalStateError,
                    .message = "audio flush failed",
                    .detail = {},
                });
            }
            blockWrites = false;
            ++snapshot.generation;
            if (resetClockToZeroOnFlush) {
                snapshot.position = 0us;
                firstWriteAfterFlush = true;
            }
        }
        cv.notify_all();
        return media_sdk::Result<void>::success();
    }
    void close() override
    {
        {
            std::lock_guard lock(mutex);
            ++closeCount;
            blockWrites = false;
        }
        cv.notify_all();
    }

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

    std::chrono::microseconds lastWritePtsSnapshot() const
    {
        std::lock_guard lock(mutex);
        return lastWritePts;
    }

    std::optional<std::chrono::microseconds> firstWritePtsSnapshot() const
    {
        std::lock_guard lock(mutex);
        if (writePts.empty())
            return std::nullopt;
        return writePts.front();
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
    std::vector<std::byte> lastWrittenBytes;
    std::vector<std::chrono::microseconds> writePts;
    double lastWritePlaybackRate = 1.0;
    std::chrono::microseconds lastWritePts { 0 };
    media_sdk::runtime::Generation lastWriteGeneration = 0;
    bool failResume = false;
    bool failFlush = false;
    bool blockWrites = false;
    bool writeBlocked = false;
    bool clockFromWrites = false;
    bool resetClockToZeroOnFlush = false;
    bool firstWriteAfterFlush = false;
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
        media_sdk::runtime::IVideoPresenterEvents* target = nullptr;
        media_sdk::runtime::PresentId completedId = 0;
        media_sdk::runtime::PresentStatus completedStatus = media_sdk::runtime::PresentStatus::Presented;
        bool shouldCompleteDuringPresent = false;
        {
            std::lock_guard lock(mutex);
            ++presentCount;
            lastFrame = std::move(frame);
            lastTiming = timing;
            lastPresentId = returnZeroPresentId ? 0 : ++nextPresentId;
            target = events;
            completedId = lastPresentId;
            completedStatus = completeDuringPresentStatus;
            shouldCompleteDuringPresent = completeDuringPresent;
        }
        if (shouldCompleteDuringPresent && target) {
            target->onPresentComplete({
                .id = completedId,
                .status = completedStatus,
                .detail = {},
                .diagnostics = {},
            });
        }
        return {
            .id = completedId,
            .status = nextStatus,
        };
    }

    void clear() override
    {
        std::lock_guard lock(mutex);
        ++clearCount;
        lastFrame = {};
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
    bool completeDuringPresent = false;
    media_sdk::runtime::PresentStatus completeDuringPresentStatus = media_sdk::runtime::PresentStatus::Presented;
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

    void onPlaybackClockTick(
        media_sdk::runtime::RuntimeTimeline timeline,
        media_sdk::runtime::ClockSnapshot clock) override
    {
        std::function<void()> callback;
        {
            std::lock_guard lock(mutex);
            lastClockTickTimeline = timeline;
            lastClockTick = clock;
            ++clockTickCount;
            callback = clockTickCallback;
        }
        if (callback)
            callback();
    }

    void onRuntimeError(media_sdk::MediaError error) override
    {
        std::lock_guard lock(mutex);
        lastError = std::move(error);
        ++errorCount;
    }

    mutable std::mutex mutex;
    media_sdk::runtime::RuntimeFallbackAction lastAction {};
    media_sdk::runtime::RuntimeTimeline lastEofTimeline {};
    media_sdk::runtime::RuntimeTimeline lastClockTickTimeline {};
    media_sdk::runtime::ClockSnapshot lastClockTick {};
    media_sdk::MediaError lastError {};
    std::function<void()> clockTickCallback;
    std::atomic_int fallbackRequestCount = 0;
    std::atomic_int eofPresentedCount = 0;
    std::atomic_int clockTickCount = 0;
    std::atomic_int errorCount = 0;
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

std::vector<std::byte> bytesFromFloats(std::vector<float> samples)
{
    std::vector<std::byte> bytes(samples.size() * sizeof(float));
    std::memcpy(bytes.data(), samples.data(), bytes.size());
    return bytes;
}

std::vector<std::byte> bytesFromInt16(std::vector<std::int16_t> samples)
{
    std::vector<std::byte> bytes(samples.size() * sizeof(std::int16_t));
    std::memcpy(bytes.data(), samples.data(), bytes.size());
    return bytes;
}

std::vector<std::byte> bytesFromInt32(std::vector<std::int32_t> samples)
{
    std::vector<std::byte> bytes(samples.size() * sizeof(std::int32_t));
    std::memcpy(bytes.data(), samples.data(), bytes.size());
    return bytes;
}

std::vector<std::byte> bytesFromUInt8(std::vector<std::uint8_t> samples)
{
    std::vector<std::byte> bytes(samples.size());
    std::memcpy(bytes.data(), samples.data(), bytes.size());
    return bytes;
}

std::vector<float> floatsFromBytes(const std::vector<std::byte>& bytes)
{
    std::vector<float> samples(bytes.size() / sizeof(float));
    std::memcpy(samples.data(), bytes.data(), samples.size() * sizeof(float));
    return samples;
}

std::vector<std::int16_t> int16FromBytes(const std::vector<std::byte>& bytes)
{
    std::vector<std::int16_t> samples(bytes.size() / sizeof(std::int16_t));
    std::memcpy(samples.data(), bytes.data(), samples.size() * sizeof(std::int16_t));
    return samples;
}

std::vector<std::int32_t> int32FromBytes(const std::vector<std::byte>& bytes)
{
    std::vector<std::int32_t> samples(bytes.size() / sizeof(std::int32_t));
    std::memcpy(samples.data(), bytes.data(), samples.size() * sizeof(std::int32_t));
    return samples;
}

std::vector<std::uint8_t> uint8FromBytes(const std::vector<std::byte>& bytes)
{
    std::vector<std::uint8_t> samples(bytes.size());
    std::memcpy(samples.data(), bytes.data(), samples.size());
    return samples;
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

media_sdk::runtime::RuntimeAudioFrame runtimeAudioWithSamples(
    media_sdk::runtime::SessionId sessionId,
    media_sdk::runtime::Generation generation,
    std::chrono::microseconds pts,
    std::vector<float> samples)
{
    return {
        .frame = makeAudioFrame(pts, bytesFromFloats(std::move(samples))),
        .sessionId = sessionId,
        .generation = generation,
    };
}

media_sdk::runtime::RuntimeAudioFrame runtimeAudioWithBytes(
    media_sdk::runtime::SessionId sessionId,
    media_sdk::runtime::Generation generation,
    std::chrono::microseconds pts,
    std::vector<std::byte> samples)
{
    return {
        .frame = makeAudioFrame(pts, std::move(samples)),
        .sessionId = sessionId,
        .generation = generation,
    };
}

std::vector<std::byte> waitForWrittenBytes(MockAudioOutput& audio, int expectedWrites)
{
    assert(waitUntil([&audio, expectedWrites]() { return audio.writeCount == expectedWrites; }));
    std::lock_guard lock(audio.mutex);
    return audio.lastWrittenBytes;
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

media_sdk::runtime::RuntimeVideoFrame trackedRuntimeVideo(
    media_sdk::runtime::SessionId sessionId,
    media_sdk::runtime::Generation generation,
    std::chrono::microseconds pts,
    std::atomic_int& releaseCount)
{
    auto storage = std::shared_ptr<int>(new int(7), [&releaseCount](int* value) {
        delete value;
        releaseCount.fetch_add(1, std::memory_order_relaxed);
    });
    return {
        .frame = media_sdk::VideoFrame({
            .width = 16,
            .height = 16,
            .pixelFormat = media_sdk::PixelFormat::Yuv420P,
            .pts = pts,
            .storage = std::move(storage),
        }),
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

void runtimePlayerConfigKeepsLegacyPositionalAudioClockField()
{
    media_sdk::runtime::RuntimePlayerConfig config {
        32,
        8,
        media_sdk::runtime::AudioFormat {},
        media_sdk::runtime::RuntimeAudioControls {},
        media_sdk::runtime::VideoOutputPolicy::PreferNative,
        media_sdk::runtime::RuntimeSyncConfig {},
        false,
    };

    assert(!config.audioClockEnabled);
    assert(config.maxSeekAudioGapFill == 2000ms);
}

void runtimeSyncConfigKeepsLegacyPositionalMaxDropField()
{
    media_sdk::runtime::RuntimeSyncConfig config {
        2ms,
        100ms,
        40ms,
        5,
    };

    assert(config.maxConsecutiveDropsBeforeForceRender == 5);
    assert(config.positionTickInterval == 100ms);
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

void openRejectsInvalidPlaybackRateBeforeOpeningOutputs()
{
    MockAudioOutput audio;
    MockPresenter presenter;
    media_sdk::runtime::RuntimePlayerConfig config;
    config.playbackRate = 3.0;
    auto player = makePlayer(audio, presenter, config);

    const auto result = player.open();
    assert(!result.ok());
    assert(result.error().code == media_sdk::MediaErrorCode::InvalidArgument);
    assert(audio.openCount == 0);
    assert(presenter.clearCount == 0);
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

void seekKeepsLastPresentedFrameUntilTargetFrameArrives()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 100ms)));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));

    player.seek(500ms);
    assert(presenter.clearCount == 0);
    assert(presenter.presentCount == 1);

    audio.setClock(500ms, 2);
    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 2, 500ms)));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 2; }));
    assert(presenter.clearCount == 0);
}

void seekClockIsAnchoredBeforeFirstAudioArrives()
{
    MockAudioOutput audio;
    audio.clockFromWrites = true;
    audio.resetClockToZeroOnFlush = true;
    audio.setClock(1500ms, 1);
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    player.seek(4763ms);

    const auto clock = player.clock();
    assert(clock.valid);
    assert(clock.generation == 2);
    assert(clock.position == 4763ms);
    assert(audio.writeCount == 0);
}

void seekAudioGapDoesNotExposeFirstAudioPtsAsImmediateClock()
{
    MockAudioOutput audio;
    audio.clockFromWrites = true;
    audio.resetClockToZeroOnFlush = true;
    audio.setClock(1500ms, 1);
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    player.seek(4763ms);
    assert(player.timeline().generation == 2);

    discardFramePushResult(player.enqueueAudio(runtimeAudio(1, 2, 6035ms)));
    assert(waitUntil([&audio]() { return audio.writeCount >= 1; }));

    const auto clock = player.clock();
    assert(clock.valid);
    assert(clock.generation == 2);
    assert(clock.position < 6035ms);
    assert(clock.position >= 4763ms);
    assert(waitUntil([&audio]() { return audio.lastWritePtsSnapshot() == 6035ms; }));
    assert(audio.writeCount > 1);
    assert(audio.firstWritePtsSnapshot() == 4763ms);
}

void pausedSeekAudioGapPreservesTargetAudioUntilResume()
{
    MockAudioOutput audio;
    audio.clockFromWrites = true;
    audio.resetClockToZeroOnFlush = true;
    audio.setClock(1500ms, 1);
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    player.pause();
    player.seek(4763ms);
    discardFramePushResult(player.enqueueAudio(runtimeAudio(1, 2, 6035ms)));
    std::this_thread::sleep_for(20ms);
    assert(audio.writeCount == 0);

    player.resume();

    assert(waitUntil([&audio]() { return audio.lastWritePtsSnapshot() == 6035ms; }));
    assert(audio.writeCount > 1);
    assert(audio.firstWritePtsSnapshot() == 4763ms);
}

void largeSeekAudioGapUsesSyntheticClockUntilAudioStarts()
{
    MockAudioOutput audio;
    audio.clockFromWrites = true;
    audio.resetClockToZeroOnFlush = true;
    audio.setClock(1500ms, 1);
    MockPresenter presenter;
    media_sdk::runtime::RuntimePlayerConfig config;
    config.maxSeekAudioGapFill = 10ms;
    auto player = makePlayer(audio, presenter, config);
    assert(player.open().ok());

    player.seek(100ms);
    discardFramePushResult(player.enqueueAudio(runtimeAudio(1, 2, 300ms)));

    assert(waitUntil([&player]() {
        const auto clock = player.clock();
        return clock.valid && clock.generation == 2 && clock.position >= 100ms && clock.position < 300ms;
    }, 100ms));
    std::this_thread::sleep_for(40ms);
    assert(audio.writeCount == 0);

    assert(waitUntil([&audio]() { return audio.lastWritePtsSnapshot() == 300ms; }));
    assert(audio.writeCount == 1);
}

void failedResumeKeepsSeekGapClockPaused()
{
    MockAudioOutput audio;
    audio.clockFromWrites = true;
    audio.resetClockToZeroOnFlush = true;
    audio.setClock(1500ms, 1);
    MockPresenter presenter;
    media_sdk::runtime::RuntimePlayerConfig config;
    config.maxSeekAudioGapFill = 10ms;
    auto player = makePlayer(audio, presenter, config);
    assert(player.open().ok());

    player.pause();
    player.seek(100ms);
    discardFramePushResult(player.enqueueAudio(runtimeAudio(1, 2, 300ms)));
    assert(waitUntil([&player]() {
        const auto clock = player.clock();
        return clock.valid && clock.generation == 2 && clock.position == 100ms;
    }));

    audio.failResume = true;
    player.resume();
    std::this_thread::sleep_for(40ms);

    const auto clock = player.clock();
    assert(clock.valid);
    assert(clock.generation == 2);
    assert(clock.position == 100ms);
    assert(audio.writeCount == 0);
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
    assert(audio.lastWritePtsSnapshot() == 42ms);
    assert(audio.lastWriteGeneration == 1);
    assert(player.diagnostics().audioQueued == 1);
    assert(player.diagnostics().audioQueueHighWatermark >= 1);
    assert(player.diagnostics().audioBackpressureCount == 0);
    assert(player.diagnostics().audioWritten == 1);
}

void audioWritesCarryConfiguredPlaybackRate()
{
    MockAudioOutput audio;
    MockPresenter presenter;
    media_sdk::runtime::RuntimePlayerConfig config;
    config.playbackRate = 1.25;
    auto player = makePlayer(audio, presenter, config);
    assert(player.open().ok());

    discardFramePushResult(player.enqueueAudio(runtimeAudio(1, 1, 42ms)));
    assert(waitUntil([&audio]() { return audio.writeCount == 1; }));
    assert(audio.lastWritePlaybackRate == 1.25);
}

void audioControlsApplyGainAndMuteBeforeAudioWrite()
{
    MockAudioOutput audio;
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    player.setAudioControls({
        .volume = 0.5f,
        .muted = false,
    });
    const auto gainResult = player.enqueueAudio(
        runtimeAudioWithSamples(1, 1, 42ms, { 1.0f, -0.5f, 0.25f, -1.0f }));
    assert(gainResult.status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);
    assert(waitUntil([&audio]() { return audio.writeCount == 1; }));
    const auto gained = floatsFromBytes(audio.lastWrittenBytes);
    assert(gained.size() == 4);
    assert(gained[0] == 0.5f);
    assert(gained[1] == -0.25f);
    assert(gained[2] == 0.125f);
    assert(gained[3] == -0.5f);

    player.setAudioControls({
        .volume = 1.0f,
        .muted = true,
    });
    const auto muteResult = player.enqueueAudio(
        runtimeAudioWithSamples(1, 1, 84ms, { 1.0f, -0.5f, 0.25f, -1.0f }));
    assert(muteResult.status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);
    assert(waitUntil([&audio]() { return audio.writeCount == 2; }));
    const auto muted = floatsFromBytes(audio.lastWrittenBytes);
    assert(muted.size() == 4);
    for (float sample : muted)
        assert(sample == 0.0f);
}

void audioControlsApplyGainForInt16Audio()
{
    MockAudioOutput audio;
    MockPresenter presenter;
    media_sdk::runtime::RuntimePlayerConfig config;
    config.audioFormat.sampleFormat = media_sdk::runtime::AudioSampleFormat::Int16;
    auto player = makePlayer(audio, presenter, config);
    assert(player.open().ok());

    player.setAudioControls({
        .volume = 0.5f,
        .muted = false,
    });
    const auto result = player.enqueueAudio(
        runtimeAudioWithBytes(1, 1, 42ms, bytesFromInt16({ 12000, -12000, 3000, -3000 })));
    assert(result.status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);

    const auto samples = int16FromBytes(waitForWrittenBytes(audio, 1));
    assert(samples.size() == 4);
    assert(samples[0] == 6000);
    assert(samples[1] == -6000);
    assert(samples[2] == 1500);
    assert(samples[3] == -1500);
}

void audioControlsApplyGainForInt32Audio()
{
    MockAudioOutput audio;
    MockPresenter presenter;
    media_sdk::runtime::RuntimePlayerConfig config;
    config.audioFormat.sampleFormat = media_sdk::runtime::AudioSampleFormat::Int32;
    auto player = makePlayer(audio, presenter, config);
    assert(player.open().ok());

    player.setAudioControls({
        .volume = 0.5f,
        .muted = false,
    });
    const auto result = player.enqueueAudio(
        runtimeAudioWithBytes(1, 1, 42ms, bytesFromInt32({ 120000, -120000, 3000, -3000 })));
    assert(result.status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);

    const auto samples = int32FromBytes(waitForWrittenBytes(audio, 1));
    assert(samples.size() == 4);
    assert(samples[0] == 60000);
    assert(samples[1] == -60000);
    assert(samples[2] == 1500);
    assert(samples[3] == -1500);
}

void audioControlsApplyGainForUInt8Audio()
{
    MockAudioOutput audio;
    MockPresenter presenter;
    media_sdk::runtime::RuntimePlayerConfig config;
    config.audioFormat.sampleFormat = media_sdk::runtime::AudioSampleFormat::UInt8;
    auto player = makePlayer(audio, presenter, config);
    assert(player.open().ok());

    player.setAudioControls({
        .volume = 0.5f,
        .muted = false,
    });
    const auto result = player.enqueueAudio(
        runtimeAudioWithBytes(1, 1, 42ms, bytesFromUInt8({ 228, 28, 128 })));
    assert(result.status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);

    const auto samples = uint8FromBytes(waitForWrittenBytes(audio, 1));
    assert(samples.size() == 3);
    assert(samples[0] == 178);
    assert(samples[1] == 78);
    assert(samples[2] == 128);
}

void audioControlsMuteUsesFormatSilence()
{
    {
        MockAudioOutput audio;
        MockPresenter presenter;
        media_sdk::runtime::RuntimePlayerConfig config;
        config.audioFormat.sampleFormat = media_sdk::runtime::AudioSampleFormat::Int16;
        auto player = makePlayer(audio, presenter, config);
        assert(player.open().ok());

        player.setAudioControls({
            .volume = 1.0f,
            .muted = true,
        });
        const auto result = player.enqueueAudio(
            runtimeAudioWithBytes(1, 1, 42ms, bytesFromInt16({ 12000, -12000 })));
        assert(result.status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);

        const auto samples = int16FromBytes(waitForWrittenBytes(audio, 1));
        assert(samples.size() == 2);
        assert(samples[0] == 0);
        assert(samples[1] == 0);
    }

    {
        MockAudioOutput audio;
        MockPresenter presenter;
        media_sdk::runtime::RuntimePlayerConfig config;
        config.audioFormat.sampleFormat = media_sdk::runtime::AudioSampleFormat::UInt8;
        auto player = makePlayer(audio, presenter, config);
        assert(player.open().ok());

        player.setAudioControls({
            .volume = 1.0f,
            .muted = true,
        });
        const auto result = player.enqueueAudio(
            runtimeAudioWithBytes(1, 1, 42ms, bytesFromUInt8({ 228, 28, 128 })));
        assert(result.status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);

        const auto samples = uint8FromBytes(waitForWrittenBytes(audio, 1));
        assert(samples.size() == 3);
        for (auto sample : samples)
            assert(sample == 128);
    }
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

void runtimeClockTickerEmitsEffectivePlaybackClock()
{
    MockAudioOutput audio;
    audio.setClock(22766ms, 1);
    MockPresenter presenter;
    MockRuntimeEvents events;
    media_sdk::runtime::RuntimePlayerConfig config;
    config.syncConfig.positionTickInterval = 10ms;
    media_sdk::runtime::RuntimePlayer player(
        config,
        {
            .audioOutput = &audio,
            .videoPresenter = &presenter,
            .events = &events,
        });
    assert(player.open().ok());

    assert(waitUntil([&events]() { return events.clockTickCount > 0; }, 200ms));
    std::lock_guard lock(events.mutex);
    assert(events.lastClockTickTimeline.sessionId == 1);
    assert(events.lastClockTickTimeline.generation == 1);
    assert(events.lastClockTick.valid);
    assert(events.lastClockTick.generation == 1);
    assert(events.lastClockTick.position == 22766ms);
}

void runtimeClockTickerPublishesVideoOnlyClockWithoutReusingAudioClock()
{
    MockAudioOutput audio;
    audio.setClock(22766ms, 1);
    MockPresenter presenter;
    MockRuntimeEvents events;
    media_sdk::runtime::RuntimePlayerConfig config;
    config.audioClockEnabled = false;
    config.syncConfig.positionTickInterval = 5ms;
    media_sdk::runtime::RuntimePlayer player(
        config,
        {
            .audioOutput = &audio,
            .videoPresenter = &presenter,
            .events = &events,
        });
    assert(player.open().ok());

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 100ms)));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));
    assert(waitUntil([&events]() { return events.clockTickCount > 0; }, 200ms));

    std::lock_guard lock(events.mutex);
    assert(events.lastClockTick.valid);
    assert(events.lastClockTick.generation == 1);
    assert(events.lastClockTick.position >= 100ms);
    assert(events.lastClockTick.position < 1s);
}

void runtimeClockTickerAllowsStopFromClockCallback()
{
    MockAudioOutput audio;
    audio.setClock(22766ms, 1);
    MockPresenter presenter;
    MockRuntimeEvents events;
    media_sdk::runtime::RuntimePlayerConfig config;
    config.syncConfig.positionTickInterval = 5ms;
    std::promise<void> stopped;
    auto stoppedFuture = stopped.get_future();
    media_sdk::runtime::RuntimePlayer* playerPtr = nullptr;
    events.clockTickCallback = [&playerPtr, &stopped]() {
        playerPtr->stop();
        stopped.set_value();
    };
    media_sdk::runtime::RuntimePlayer player(
        config,
        {
            .audioOutput = &audio,
            .videoPresenter = &presenter,
            .events = &events,
        });
    playerPtr = &player;
    assert(player.open().ok());

    assert(stoppedFuture.wait_for(500ms) == std::future_status::ready);
    assert(player.timeline().sessionId == 0);
    assert(audio.closeCount == 1);
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

void skippedPresentConsumesFrameWithoutBackpressuringNextFrame()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    presenter.maxPendingFrames = 1;
    presenter.nextStatus = media_sdk::runtime::PresentStatus::Skipped;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 100ms)));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 101ms)));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 2; }));

    const auto diagnostics = player.diagnostics();
    assert(diagnostics.videoPresented == 2);
    assert(diagnostics.nativeFallbacks == 0);
}

void earlyPresenterCompletionBeforeTrackDoesNotDeadlockBackpressure()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    presenter.maxPendingFrames = 1;
    presenter.completeDuringPresent = true;
    presenter.completeDuringPresentStatus = media_sdk::runtime::PresentStatus::Skipped;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 100ms)));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 101ms)));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 2; }));

    const auto diagnostics = player.diagnostics();
    assert(diagnostics.videoPresented == 2);
    assert(diagnostics.nativeFallbacks == 0);
}

void earlyPresenterFailureBeforeTrackStillRequestsFallback()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    presenter.completeDuringPresent = true;
    presenter.completeDuringPresentStatus = media_sdk::runtime::PresentStatus::DeviceLost;
    MockRuntimeEvents events;
    auto player = makePlayer(audio, presenter, events);
    assert(player.open().ok());

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 1, 100ms, media_sdk::PixelFormat::Native)));

    assert(waitUntil([&events]() { return events.fallbackRequestCount == 1; }));
    assert(player.diagnostics().nativeFallbacks == 1);
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

void eofBackpressureIsReportedInDiagnostics()
{
    MockAudioOutput audio;
    audio.blockAudioWrites();
    MockPresenter presenter;
    media_sdk::runtime::RuntimePlayerConfig config;
    config.audioQueueCapacity = 1;
    auto player = makePlayer(audio, presenter, config);
    assert(player.open().ok());

    discardFramePushResult(player.enqueueAudio(runtimeAudio(1, 1, 10ms)));
    assert(audio.waitForBlockedWrite());
    discardFramePushResult(player.enqueueAudio(runtimeAudio(1, 1, 20ms)));

    auto future = std::async(std::launch::async, [&player]() {
        player.enqueueEndOfStream(1, 1);
    });
    std::this_thread::sleep_for(20ms);
    assert(future.wait_for(0ms) == std::future_status::timeout);

    audio.releaseAudioWrites();
    assert(future.wait_for(500ms) == std::future_status::ready);

    const auto diagnostics = player.diagnostics();
    assert(diagnostics.eofAccepted == 1);
    assert(diagnostics.audioBackpressureCount >= 1);
    assert(diagnostics.decodeFramePushWaitUs > 0);
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

void seekReportsAudioFlushFailure()
{
    MockAudioOutput audio;
    MockPresenter presenter;
    MockRuntimeEvents events;
    auto player = makePlayer(audio, presenter, events);
    assert(player.open().ok());

    audio.failFlush = true;
    player.seek(500ms);

    assert(events.errorCount == 1);
    assert(events.lastError.message == "audio flush failed");
}

void pausedSeekPresentsOnePrerollFrameWithoutResumingAudio()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    player.pause();
    assert(audio.pauseCount == 1);

    player.seek(500ms);
    audio.setClock(500ms, 2);
    const auto result = player.enqueueVideo(runtimeVideo(1, 2, 500ms));

    assert(result.status == media_sdk::runtime::RuntimeFramePushStatus::Accepted);
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));
    assert(audio.resumeCount == 1);
    assert(presenter.timing().clock == 500ms);
}

void pausedPlaybackCancelsLateVideoPushWithoutDecodeError()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    player.pause();
    const auto result = player.enqueueVideo(runtimeVideo(1, 1, 100ms));

    assert(result.status == media_sdk::runtime::RuntimeFramePushStatus::Cancelled);
    std::this_thread::sleep_for(10ms);
    assert(presenter.presentCount == 0);
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

void stopUnblocksPausedAudioWriteBeforeJoiningWorkers()
{
    MockAudioOutput audio;
    audio.blockAudioWrites();
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    discardFramePushResult(player.enqueueAudio(runtimeAudio(1, 1, 10ms)));
    assert(audio.waitForBlockedWrite());
    player.pause();

    auto future = std::async(std::launch::async, [&player]() {
        player.stop();
    });

    assert(future.wait_for(500ms) == std::future_status::ready);
    assert(audio.pauseCount >= 1);
    assert(audio.flushCount == 1);
    assert(audio.closeCount == 1);
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

void seekKeepsOldFrameStorageUntilReplacementArrives()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());
    std::atomic_int releaseCount { 0 };

    discardFramePushResult(player.enqueueVideo(trackedRuntimeVideo(1, 1, 100ms, releaseCount)));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));

    player.seek(200ms);
    assert(releaseCount.load(std::memory_order_relaxed) == 0);

    audio.setClock(200ms, 2);
    player.completeSeek(1, 2);
    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 2, 200ms)));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 2; }));
    assert(waitUntil([&releaseCount]() {
        return releaseCount.load(std::memory_order_relaxed) == 1;
    }));
}

void eofDrainKeepsLastFrameUntilStopThenReleasesIt()
{
    MockAudioOutput audio;
    audio.setClock(100ms, 1);
    MockPresenter presenter;
    MockRuntimeEvents events;
    auto player = makePlayer(audio, presenter, events);
    assert(player.open().ok());
    std::atomic_int releaseCount { 0 };

    discardFramePushResult(player.enqueueVideo(trackedRuntimeVideo(1, 1, 100ms, releaseCount)));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));
    player.enqueueEndOfStream(1, 1);
    presenter.complete(presenter.lastId(), media_sdk::runtime::PresentStatus::Presented);
    assert(waitUntil([&events]() { return events.eofPresentedCount == 1; }));
    assert(releaseCount.load(std::memory_order_relaxed) == 0);

    player.stop();
    assert(releaseCount.load(std::memory_order_relaxed) == 1);
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

void fallbackDuringSeekGapUsesEffectiveClockResumePosition()
{
    MockAudioOutput audio;
    audio.clockFromWrites = true;
    audio.resetClockToZeroOnFlush = true;
    audio.setClock(1500ms, 1);
    MockPresenter presenter;
    MockRuntimeEvents events;
    media_sdk::runtime::RuntimePlayerConfig config;
    config.maxSeekAudioGapFill = 10ms;
    media_sdk::runtime::RuntimePlayer player(
        config,
        {
            .audioOutput = &audio,
            .videoPresenter = &presenter,
            .events = &events,
        });
    assert(player.open().ok());

    player.seek(100ms);
    discardFramePushResult(player.enqueueAudio(runtimeAudio(1, 2, 500ms)));
    assert(waitUntil([&player]() {
        const auto clock = player.clock();
        return clock.valid && clock.generation == 2 && clock.position >= 120ms && clock.position < 200ms;
    }));

    discardFramePushResult(player.enqueueVideo(runtimeVideo(1, 2, 260ms, media_sdk::PixelFormat::Native)));
    assert(waitUntil([&presenter]() { return presenter.presentCount == 1; }));
    presenter.complete(presenter.lastId(), media_sdk::runtime::PresentStatus::DeviceLost);

    assert(waitUntil([&events]() { return events.fallbackRequestCount == 1; }));
    std::lock_guard lock(events.mutex);
    assert(events.lastAction.resumePosition >= 100ms);
    assert(events.lastAction.resumePosition < 500ms);
}

void fallbackPendingRejectsOldGenerationFramesAsStale()
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

    const auto staleResult = player.enqueueVideo(runtimeVideo(1, 1, 100ms));
    assert(staleResult.status == media_sdk::runtime::RuntimeFramePushStatus::RejectedGeneration);
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
    assert(presenter.clearCount == 0);
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
    runtimePlayerConfigKeepsLegacyPositionalAudioClockField();
    runtimeSyncConfigKeepsLegacyPositionalMaxDropField();
    openStartsNewSessionAndResetsQueues();
    openFailsAndClosesAudioWhenResumeFails();
    openRejectsInvalidPlaybackRateBeforeOpeningOutputs();
    timelineTracksSeekGeneration();
    seekKeepsLastPresentedFrameUntilTargetFrameArrives();
    seekClockIsAnchoredBeforeFirstAudioArrives();
    seekAudioGapDoesNotExposeFirstAudioPtsAsImmediateClock();
    pausedSeekAudioGapPreservesTargetAudioUntilResume();
    largeSeekAudioGapUsesSyntheticClockUntilAudioStarts();
    failedResumeKeepsSeekGapClockPaused();
    audioFramesAreWrittenToInjectedAudioOutput();
    audioWritesCarryConfiguredPlaybackRate();
    audioControlsApplyGainAndMuteBeforeAudioWrite();
    audioControlsApplyGainForInt16Audio();
    audioControlsApplyGainForInt32Audio();
    audioControlsApplyGainForUInt8Audio();
    audioControlsMuteUsesFormatSilence();
    audioQueueBackpressureIsReportedInDiagnostics();
    videoFramesAreScheduledAgainstAudioClock();
    videoWaitDecisionDelaysAndEventuallyPresentsSameFrame();
    videoOnlyPlaybackUsesMonotonicClockWhenAudioClockIsDisabled();
    runtimeClockTickerEmitsEffectivePlaybackClock();
    runtimeClockTickerPublishesVideoOnlyClockWithoutReusingAudioClock();
    runtimeClockTickerAllowsStopFromClockCallback();
    presenterBackpressureWaitsForCompletionBeforeSubmittingNextFrame();
    skippedPresentConsumesFrameWithoutBackpressuringNextFrame();
    earlyPresenterCompletionBeforeTrackDoesNotDeadlockBackpressure();
    earlyPresenterFailureBeforeTrackStillRequestsFallback();
    presenterBackpressureSeekCancelsWaitingOldGenerationFrame();
    eofCompletesOnlyAfterAudioAndVideoDrain();
    eofBackpressureIsReportedInDiagnostics();
    seekInvalidatesOldGenerationFramesAndCompletions();
    seekReportsAudioFlushFailure();
    pausedSeekPresentsOnePrerollFrameWithoutResumingAudio();
    pausedPlaybackCancelsLateVideoPushWithoutDecodeError();
    stopAbortsQueuesPausesAudioClearsPresenterAndReturnsIdle();
    stopUnblocksPausedAudioWriteBeforeJoiningWorkers();
    nativePresenterFailureRunsFullFallbackTransition();
    nativeDiagnosticsTrackZeroCopySuccessWithoutCpuCopy();
    seekKeepsOldFrameStorageUntilReplacementArrives();
    eofDrainKeepsLastFrameUntilStopThenReleasesIt();
    currentGenerationDeviceLostPausesAudioFlushesQueuesClearsPresenterAndRequestsCpuOnlyDecode();
    fallbackDuringSeekGapUsesEffectiveClockResumePosition();
    queuedPresenterResultWithZeroIdTriggersFallback();
    oldGenerationDeviceLostDoesNotInterruptCurrentPlayback();
    fallbackSeekCompletionResumesAudioAndVideoScheduling();
    fallbackPendingRejectsOldGenerationFramesAsStale();
}
