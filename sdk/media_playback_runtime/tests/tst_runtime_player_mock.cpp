#include "media_sdk/runtime/RuntimePlayer.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace {

class MockAudioOutput final : public media_sdk::runtime::IAudioOutput {
public:
    media_sdk::Result<void> open(const media_sdk::runtime::AudioFormat& format) override
    {
        lastFormat = format;
        ++openCount;
        return media_sdk::Result<void>::success();
    }

    media_sdk::Result<void> write(media_sdk::runtime::AudioBufferView buffer) override
    {
        ++writeCount;
        writtenBytes += buffer.bytes.size();
        lastWritePts = buffer.pts;
        lastWriteGeneration = buffer.generation;
        return media_sdk::Result<void>::success();
    }

    media_sdk::runtime::ClockSnapshot clock() const override
    {
        return snapshot;
    }

    void pause() override { ++pauseCount; }
    void resume() override { ++resumeCount; }
    void flush() override { ++flushCount; ++snapshot.generation; }
    void close() override { ++closeCount; }

    media_sdk::runtime::AudioFormat lastFormat {};
    media_sdk::runtime::ClockSnapshot snapshot {
        .position = 0us,
        .hardwareLatency = 0us,
        .queuedDuration = 0us,
        .generation = 1,
        .valid = true,
        .paused = false,
    };
    int openCount = 0;
    int writeCount = 0;
    int pauseCount = 0;
    int resumeCount = 0;
    int flushCount = 0;
    int closeCount = 0;
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
            .maxPendingFrames = 2,
        };
    }

    void setEvents(media_sdk::runtime::IVideoPresenterEvents* nextEvents) override
    {
        events = nextEvents;
    }

    media_sdk::runtime::PresentResult present(
        media_sdk::VideoFrame frame,
        media_sdk::runtime::PresentTiming timing) override
    {
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

    void complete(media_sdk::runtime::PresentId id, media_sdk::runtime::PresentStatus status)
    {
        if (events) {
            events->onPresentComplete({
                .id = id,
                .status = status,
                .detail = {},
            });
        }
    }

    media_sdk::runtime::IVideoPresenterEvents* events = nullptr;
    media_sdk::runtime::PresentStatus nextStatus = media_sdk::runtime::PresentStatus::Queued;
    media_sdk::VideoFrame lastFrame {};
    media_sdk::runtime::PresentTiming lastTiming {};
    media_sdk::runtime::PresentId nextPresentId = 0;
    media_sdk::runtime::PresentId lastPresentId = 0;
    int presentCount = 0;
    int clearCount = 0;
};

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

void openStartsNewSessionAndResetsQueues()
{
    MockAudioOutput audio;
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);

    assert(player.open().ok());
    player.enqueueAudio(runtimeAudio(0, 0, 0ms));
    assert(audio.writeCount == 0);

    player.enqueueAudio(runtimeAudio(1, 1, 10ms));
    assert(audio.writeCount == 1);

    assert(player.open().ok());
    player.enqueueAudio(runtimeAudio(1, 1, 20ms));
    assert(audio.writeCount == 1);
    player.enqueueAudio(runtimeAudio(2, 1, 20ms));
    assert(audio.writeCount == 2);
}

void audioFramesAreWrittenToInjectedAudioOutput()
{
    MockAudioOutput audio;
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    player.enqueueAudio(runtimeAudio(1, 1, 42ms));
    assert(audio.writeCount == 1);
    assert(audio.writtenBytes == 128);
    assert(audio.lastWritePts == 42ms);
    assert(audio.lastWriteGeneration == 1);
    assert(player.diagnostics().audioQueued == 1);
    assert(player.diagnostics().audioWritten == 1);
}

void videoFramesAreScheduledAgainstAudioClock()
{
    MockAudioOutput audio;
    audio.snapshot.position = 100ms;
    audio.snapshot.generation = 1;
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    player.enqueueVideo(runtimeVideo(1, 1, 101ms));
    assert(presenter.presentCount == 1);
    assert(presenter.lastTiming.clock == 100ms);

    audio.snapshot.position = 250ms;
    player.enqueueVideo(runtimeVideo(1, 1, 1ms));
    assert(presenter.presentCount == 1);
    assert(player.diagnostics().videoDroppedLate == 1);
}

void eofCompletesOnlyAfterAudioAndVideoDrain()
{
    MockAudioOutput audio;
    audio.snapshot.position = 100ms;
    audio.snapshot.generation = 1;
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    player.enqueueAudio(runtimeAudio(1, 1, 10ms));
    player.enqueueVideo(runtimeVideo(1, 1, 100ms));
    player.enqueueEndOfStream(1, 1);

    const auto diagnostics = player.diagnostics();
    assert(diagnostics.audioWritten == 1);
    assert(diagnostics.videoPresented == 1);
    assert(diagnostics.eofAccepted == 1);
    assert(diagnostics.eofPresented == 1);
}

void seekInvalidatesOldGenerationFramesAndCompletions()
{
    MockAudioOutput audio;
    audio.snapshot.position = 100ms;
    audio.snapshot.generation = 1;
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    player.enqueueVideo(runtimeVideo(1, 1, 100ms, media_sdk::PixelFormat::Native));
    const auto oldPresentId = presenter.lastPresentId;
    assert(presenter.presentCount == 1);

    player.seek(500ms);
    presenter.complete(oldPresentId, media_sdk::runtime::PresentStatus::DeviceLost);
    assert(player.diagnostics().nativeFallbacks == 0);

    player.enqueueVideo(runtimeVideo(1, 1, 500ms));
    assert(presenter.presentCount == 1);

    audio.snapshot.generation = 2;
    audio.snapshot.position = 500ms;
    player.enqueueVideo(runtimeVideo(1, 2, 500ms));
    assert(presenter.presentCount == 2);
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
    audio.snapshot.position = 100ms;
    audio.snapshot.generation = 1;
    MockPresenter presenter;
    auto player = makePlayer(audio, presenter);
    assert(player.open().ok());

    player.enqueueVideo(runtimeVideo(1, 1, 100ms, media_sdk::PixelFormat::Native));
    assert(presenter.presentCount == 1);

    presenter.complete(presenter.lastPresentId, media_sdk::runtime::PresentStatus::DeviceLost);
    const auto diagnostics = player.diagnostics();
    assert(diagnostics.nativeFallbacks == 1);
    assert(diagnostics.queueAbortCount == 2);
    assert(audio.pauseCount == 1);
    assert(audio.flushCount == 1);
    assert(presenter.clearCount == 1);

    player.enqueueVideo(runtimeVideo(1, 1, 100ms));
    assert(presenter.presentCount == 1);
}

} // namespace

int main()
{
    openStartsNewSessionAndResetsQueues();
    audioFramesAreWrittenToInjectedAudioOutput();
    videoFramesAreScheduledAgainstAudioClock();
    eofCompletesOnlyAfterAudioAndVideoDrain();
    seekInvalidatesOldGenerationFramesAndCompletions();
    stopAbortsQueuesPausesAudioClearsPresenterAndReturnsIdle();
    nativePresenterFailureRunsFullFallbackTransition();
}
