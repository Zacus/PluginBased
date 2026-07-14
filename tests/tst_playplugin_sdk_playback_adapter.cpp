#include "playback/SdkPlaybackAdapter.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QUrl>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace {

media_sdk::PlayerEvent errorEvent(std::uint64_t sessionId, std::uint64_t generation)
{
    return {
        .metadata = { .sessionId = sessionId, .generation = generation },
        .payload = media_sdk::ErrorEvent {
            .error = {
                .code = media_sdk::MediaErrorCode::DecodeFailed,
                .message = "stale session error",
            },
        },
    };
}

media_sdk::PlayerEvent playbackRateEvent(double playbackRate)
{
    return {
        .metadata = { .sessionId = 10, .generation = 2 },
        .payload = media_sdk::PlaybackRateChangedEvent {
            .playbackRate = playbackRate,
            .position = 250ms,
        },
    };
}

class FakeTempoProcessor final : public media_sdk::runtime::IAudioTempoProcessor {
public:
    media_sdk::Result<void> configure(
        const media_sdk::runtime::AudioFormat&,
        double) override
    {
        return media_sdk::Result<void>::success();
    }

    media_sdk::Result<media_sdk::runtime::AudioTempoOutput> process(
        media_sdk::runtime::AudioBufferView) override
    {
        return media_sdk::Result<media_sdk::runtime::AudioTempoOutput>::success({});
    }

    media_sdk::Result<media_sdk::runtime::AudioTempoOutput> drain() override
    {
        return media_sdk::Result<media_sdk::runtime::AudioTempoOutput>::success({});
    }

    void reset() noexcept override {}
};

class FakeSession final : public ISdkPlaybackSession {
public:
    FakeSession(media_sdk::session::PlaybackSessionConfig config,
                media_sdk::session::PlaybackSessionDependencies dependencies)
        : events(dependencies.events)
        , lastAudioControls(config.runtime.audioControls)
        , preferNativeVideoFrames(config.preferNativeVideoFrames)
        , corePreferNativeVideoFrames(config.core.preferNativeVideoFrames)
        , coreDecoderBufferPoolEnabled(config.core.enableDecoderBufferPool)
        , currentPlaybackRate(config.runtime.playbackRate)
        , hasAudioTempoProcessor(dependencies.audioTempoProcessor != nullptr)
    {
    }

    media_sdk::Result<void> open(const std::filesystem::path& path) override
    {
        openedPath = path;
        return media_sdk::Result<void>::success();
    }

    void play() override
    {
        ++playCount;
    }

    void pause() override
    {
        ++pauseCount;
    }

    void stop() override
    {
        ++stopCount;
        if (emitErrorOnStop && events)
            events->onEvent(errorEvent(10, 1));
    }

    media_sdk::Result<void> seek(std::chrono::milliseconds position,
                                  media_sdk::SeekPlaybackMode mode) override
    {
        lastSeek = position;
        lastSeekMode = mode;
        return media_sdk::Result<void>::success();
    }

    media_sdk::Result<void> setPlaybackRate(double playbackRate) override
    {
        ++rateChangeCount;
        lastRequestedPlaybackRate = playbackRate;
        if (failRateChange) {
            return media_sdk::Result<void>::failure({
                .code = media_sdk::MediaErrorCode::InternalStateError,
                .message = "rate change failed",
                .detail = {},
            });
        }
        currentPlaybackRate = playbackRate;
        return media_sdk::Result<void>::success();
    }

    double playbackRate() const override
    {
        return currentPlaybackRate;
    }

    void setAudioControls(media_sdk::runtime::RuntimeAudioControls controls) override
    {
        lastAudioControls = controls;
        ++audioControlCount;
    }

    void notifyNativeRenderingFailed() override
    {
        ++nativeRenderingFailureCount;
    }

    media_sdk::runtime::RuntimeTimeline timeline() const override
    {
        return { .sessionId = 100, .generation = 7 };
    }

    void emitPlaybackRateChanged(double playbackRate)
    {
        currentPlaybackRate = playbackRate;
        events->onEvent(playbackRateEvent(playbackRate));
    }

    void emitError()
    {
        events->onEvent(errorEvent(10, 2));
    }

    media_sdk::session::ISessionEvents* events = nullptr;
    std::filesystem::path openedPath;
    int playCount = 0;
    int pauseCount = 0;
    int stopCount = 0;
    int audioControlCount = 0;
    int nativeRenderingFailureCount = 0;
    bool emitErrorOnStop = false;
    bool preferNativeVideoFrames = false;
    bool corePreferNativeVideoFrames = false;
    bool coreDecoderBufferPoolEnabled = false;
    bool hasAudioTempoProcessor = false;
    bool failRateChange = false;
    int rateChangeCount = 0;
    double currentPlaybackRate = 1.0;
    double lastRequestedPlaybackRate = 1.0;
    std::chrono::milliseconds lastSeek { 0 };
    media_sdk::SeekPlaybackMode lastSeekMode = media_sdk::SeekPlaybackMode::PreservePlaybackState;
    media_sdk::runtime::RuntimeAudioControls lastAudioControls {};
};

struct AdapterHarness {
    FakeTempoProcessor tempoProcessor;
    std::vector<FakeSession*> sessions;
    SdkPlaybackAdapter adapter {
        nullptr,
        nullptr,
        &tempoProcessor,
        [this](media_sdk::session::PlaybackSessionConfig config,
               media_sdk::session::PlaybackSessionDependencies dependencies) {
            auto session = std::make_unique<FakeSession>(config, dependencies);
            sessions.push_back(session.get());
            return session;
        }
    };
};

void openAfterPauseStartsNewSessionPlaying()
{
    AdapterHarness harness;

    harness.adapter.setPaused(true);
    harness.adapter.openFile(QUrl::fromLocalFile("/tmp/a.mov"));

    assert(harness.sessions.size() == 1);
    assert(harness.sessions[0]->playCount == 1);
    assert(harness.sessions[0]->pauseCount == 0);
}

void stopTimeEventsFromOldSessionAreIgnoredAfterReopen()
{
    AdapterHarness harness;
    QSignalSpy errors(&harness.adapter, &SdkPlaybackAdapter::errorOccurred);

    harness.adapter.openFile(QUrl::fromLocalFile("/tmp/a.mov"));
    assert(harness.sessions.size() == 1);
    harness.sessions[0]->emitErrorOnStop = true;

    harness.adapter.openFile(QUrl::fromLocalFile("/tmp/b.mov"));
    QCoreApplication::processEvents();

    assert(harness.sessions.size() == 2);
    assert(errors.count() == 0);
}

void audioControlsApplyToFutureAndCurrentSession()
{
    AdapterHarness harness;

    harness.adapter.setVolume(0.25f);
    harness.adapter.setMuted(true);
    harness.adapter.openFile(QUrl::fromLocalFile("/tmp/a.mov"));

    assert(harness.sessions.size() == 1);
    assert(harness.sessions[0]->lastAudioControls.volume == 0.25f);
    assert(harness.sessions[0]->lastAudioControls.muted);

    harness.adapter.setVolume(0.75f);
    harness.adapter.setMuted(false);

    assert(harness.sessions[0]->audioControlCount == 2);
    assert(harness.sessions[0]->lastAudioControls.volume == 0.75f);
    assert(!harness.sessions[0]->lastAudioControls.muted);
}

void resumeSeekPassesPlaybackIntentToSession()
{
    AdapterHarness harness;

    harness.adapter.openFile(QUrl::fromLocalFile("/tmp/a.mov"));
    harness.adapter.seek(23678, 1, true);

    assert(harness.sessions.size() == 1);
    assert(harness.sessions[0]->lastSeek == 23678ms);
    assert(harness.sessions[0]->lastSeekMode == media_sdk::SeekPlaybackMode::ResumePlayback);
}

void nativeRenderingFailureNotifiesCurrentSessionAndDisablesFutureNative()
{
    AdapterHarness harness;

    harness.adapter.setVideoToolboxDirectRenderingEnabled(true);
    harness.adapter.openFile(QUrl::fromLocalFile("/tmp/a.mov"));
    assert(harness.sessions.size() == 1);
    assert(harness.sessions[0]->preferNativeVideoFrames);
    assert(harness.sessions[0]->corePreferNativeVideoFrames);
    assert(harness.sessions[0]->coreDecoderBufferPoolEnabled);

    harness.adapter.notifyNativeRenderingFailed();
    assert(harness.sessions[0]->nativeRenderingFailureCount == 1);

    harness.adapter.openFile(QUrl::fromLocalFile("/tmp/b.mov"));
    assert(harness.sessions.size() == 2);
    assert(!harness.sessions[1]->preferNativeVideoFrames);
    assert(!harness.sessions[1]->corePreferNativeVideoFrames);
    assert(harness.sessions[1]->coreDecoderBufferPoolEnabled);
}

void playbackRateAppliesToFutureSession()
{
    AdapterHarness harness;
    QSignalSpy rates(&harness.adapter, &SdkPlaybackAdapter::playbackRateChanged);

    harness.adapter.setPlaybackRate(1.5);
    assert(rates.count() == 1);
    assert(harness.adapter.playbackRate() == 1.5);

    harness.adapter.openFile(QUrl::fromLocalFile("/tmp/a.mov"));
    assert(harness.sessions[0]->currentPlaybackRate == 1.5);
    assert(harness.sessions[0]->hasAudioTempoProcessor);
}

void playbackRateWaitsForSdkConfirmationAndPersistsAcrossReopen()
{
    AdapterHarness harness;
    QSignalSpy rates(&harness.adapter, &SdkPlaybackAdapter::playbackRateChanged);
    harness.adapter.openFile(QUrl::fromLocalFile("/tmp/a.mov"));

    harness.adapter.setPlaybackRate(1.25);
    harness.adapter.setPlaybackRate(1.5);
    assert(harness.sessions[0]->rateChangeCount == 1);
    assert(harness.sessions[0]->lastRequestedPlaybackRate == 1.25);
    assert(rates.count() == 0);
    assert(harness.adapter.playbackRate() == 1.0);

    harness.sessions[0]->emitPlaybackRateChanged(1.25);
    QCoreApplication::processEvents();
    assert(rates.count() == 1);
    assert(harness.adapter.playbackRate() == 1.25);

    harness.adapter.openFile(QUrl::fromLocalFile("/tmp/b.mov"));
    assert(harness.sessions[1]->currentPlaybackRate == 1.25);
}

void playbackRateFailureRollsBackToConfirmedValue()
{
    AdapterHarness harness;
    QSignalSpy rates(&harness.adapter, &SdkPlaybackAdapter::playbackRateChanged);
    QSignalSpy errors(&harness.adapter, &SdkPlaybackAdapter::errorOccurred);
    harness.adapter.openFile(QUrl::fromLocalFile("/tmp/a.mov"));
    harness.sessions[0]->failRateChange = true;

    harness.adapter.setPlaybackRate(1.5);

    assert(rates.count() == 1);
    assert(errors.count() == 1);
    assert(harness.adapter.playbackRate() == 1.0);
}

void asynchronousRateErrorRollsBackPendingSelection()
{
    AdapterHarness harness;
    QSignalSpy rates(&harness.adapter, &SdkPlaybackAdapter::playbackRateChanged);
    harness.adapter.openFile(QUrl::fromLocalFile("/tmp/a.mov"));
    harness.adapter.setPlaybackRate(1.5);

    harness.sessions[0]->emitError();
    QCoreApplication::processEvents();

    assert(rates.count() == 1);
    assert(harness.adapter.playbackRate() == 1.0);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    openAfterPauseStartsNewSessionPlaying();
    stopTimeEventsFromOldSessionAreIgnoredAfterReopen();
    audioControlsApplyToFutureAndCurrentSession();
    resumeSeekPassesPlaybackIntentToSession();
    nativeRenderingFailureNotifiesCurrentSessionAndDisablesFutureNative();
    playbackRateAppliesToFutureSession();
    playbackRateWaitsForSdkConfirmationAndPersistsAcrossReopen();
    playbackRateFailureRollsBackToConfirmedValue();
    asynchronousRateErrorRollsBackPendingSelection();
    return 0;
}
