#include "media_sdk/runtime/RuntimePlayer.h"

#include "AudioSilence.h"
#include "AvSyncScheduler.h"
#include "NativeFallbackController.h"
#include "PresentTracker.h"
#include "RuntimeFrameQueue.h"
#include "SeekClockAnchor.h"
#include "media_sdk/Error.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace media_sdk::runtime {
namespace {

using SteadyClock = std::chrono::steady_clock;

AvSyncConfig toAvSyncConfig(RuntimeSyncConfig config)
{
    return {
        .submitLeadTime = config.submitLeadTime,
        .lateDropThreshold = config.lateDropThreshold,
        .maxScheduledWait = config.maxScheduledWait,
        .maxConsecutiveDropsBeforeForceRender = config.maxConsecutiveDropsBeforeForceRender,
    };
}

class RuntimeClockTicker final {
public:
    using Callback = std::function<void()>;

    RuntimeClockTicker(std::chrono::microseconds interval, Callback callback)
        : m_state(std::make_shared<State>(interval, std::move(callback)))
    {
    }

    ~RuntimeClockTicker()
    {
        stop();
    }

    RuntimeClockTicker(const RuntimeClockTicker&) = delete;
    RuntimeClockTicker& operator=(const RuntimeClockTicker&) = delete;

    void start()
    {
        stop();
        auto state = m_state;
        if (state->interval <= std::chrono::microseconds::zero() || !state->callback)
            return;

        std::uint64_t runId = 0;
        {
            std::lock_guard lock(state->mutex);
            state->running = true;
            runId = ++state->runId;
        }
        state->thread = std::thread([state, runId]() { run(state, runId); });
    }

    void stop()
    {
        auto state = m_state;
        {
            std::lock_guard lock(state->mutex);
            state->running = false;
            ++state->runId;
        }
        state->cv.notify_all();
        if (!state->thread.joinable())
            return;
        if (state->thread.get_id() == std::this_thread::get_id()) {
            // clock tick 回调允许调用 RuntimePlayer::stop()；此时不能 join 自己。
            // 线程函数持有 state 的 shared_ptr，detach 后会在本轮回调返回后自行退出。
            state->thread.detach();
            return;
        }
        state->thread.join();
    }

private:
    struct State {
        State(std::chrono::microseconds initialInterval, Callback initialCallback)
            : interval(initialInterval)
            , callback(std::move(initialCallback))
        {
        }

        std::chrono::microseconds interval { std::chrono::milliseconds(100) };
        Callback callback;
        std::mutex mutex;
        std::condition_variable cv;
        std::thread thread;
        bool running = false;
        std::uint64_t runId = 0;
    };

    static void run(std::shared_ptr<State> state, std::uint64_t runId)
    {
        std::unique_lock lock(state->mutex);
        while (state->running && state->runId == runId) {
            if (state->cv.wait_for(lock, state->interval, [&state, runId]() {
                    return !state->running || state->runId != runId;
                })) {
                break;
            }
            lock.unlock();
            state->callback();
            lock.lock();
        }
    }

    std::shared_ptr<State> m_state;
};

bool isFailureStatus(PresentStatus status)
{
    return status == PresentStatus::UnsupportedNativeHandle
        || status == PresentStatus::DeviceLost
        || status == PresentStatus::Failed;
}

Result<void> dependencyError(const char* message)
{
    return Result<void>::failure({
        .code = MediaErrorCode::InternalStateError,
        .message = message,
        .detail = {},
    });
}

RuntimeAudioControls sanitizeAudioControls(RuntimeAudioControls controls)
{
    if (!std::isfinite(controls.volume))
        controls.volume = 1.0f;
    controls.volume = std::clamp(controls.volume, 0.0f, 1.0f);
    return controls;
}

template<typename SampleType>
bool sampleBytesAreAligned(std::span<const std::byte> samples)
{
    return samples.size() % sizeof(SampleType) == 0;
}

template<typename SampleType>
SampleType readSample(const std::byte* source)
{
    SampleType sample {};
    std::memcpy(&sample, source, sizeof(sample));
    return sample;
}

template<typename SampleType>
void writeSample(std::byte* target, SampleType sample)
{
    std::memcpy(target, &sample, sizeof(sample));
}

template<typename SampleType>
void applySignedIntegralGain(std::vector<std::byte>& output, float volume)
{
    if (!sampleBytesAreAligned<SampleType>(output))
        return;

    for (std::size_t offset = 0; offset < output.size(); offset += sizeof(SampleType)) {
        const auto sample = readSample<SampleType>(output.data() + offset);
        const auto scaled = std::llround(static_cast<double>(sample) * static_cast<double>(volume));
        const auto clamped = std::clamp(
            scaled,
            static_cast<long long>(std::numeric_limits<SampleType>::min()),
            static_cast<long long>(std::numeric_limits<SampleType>::max()));
        writeSample(output.data() + offset, static_cast<SampleType>(clamped));
    }
}

void applyUInt8Gain(std::vector<std::byte>& output, float volume)
{
    constexpr int silence = 128;
    for (std::byte& byte : output) {
        const auto sample = static_cast<int>(std::to_integer<std::uint8_t>(byte));
        const auto centered = sample - silence;
        const auto scaled = silence
            + std::llround(static_cast<double>(centered) * static_cast<double>(volume));
        byte = static_cast<std::byte>(std::clamp(scaled, 0LL, 255LL));
    }
}

void applyFloat32Gain(std::vector<std::byte>& output, float volume)
{
    if (!sampleBytesAreAligned<float>(output))
        return;

    for (std::size_t offset = 0; offset < output.size(); offset += sizeof(float)) {
        const auto sample = readSample<float>(output.data() + offset) * volume;
        writeSample(output.data() + offset, sample);
    }
}

void fillSilence(std::vector<std::byte>& output, AudioSampleFormat sampleFormat)
{
    if (sampleFormat == AudioSampleFormat::UInt8) {
        std::fill(output.begin(), output.end(), static_cast<std::byte>(128));
        return;
    }
    std::fill(output.begin(), output.end(), std::byte { 0 });
}

std::vector<std::byte> applyAudioControls(std::span<const std::byte> samples,
                                          RuntimeAudioControls controls,
                                          AudioSampleFormat sampleFormat)
{
    std::vector<std::byte> output(samples.begin(), samples.end());
    if (output.empty())
        return output;

    if (controls.muted || controls.volume == 0.0f) {
        fillSilence(output, sampleFormat);
        return output;
    }

    switch (sampleFormat) {
    case AudioSampleFormat::UInt8:
        applyUInt8Gain(output, controls.volume);
        break;
    case AudioSampleFormat::Int16:
        applySignedIntegralGain<std::int16_t>(output, controls.volume);
        break;
    case AudioSampleFormat::Int32:
        applySignedIntegralGain<std::int32_t>(output, controls.volume);
        break;
    case AudioSampleFormat::Float32:
    case AudioSampleFormat::Float32Planar:
        applyFloat32Gain(output, controls.volume);
        break;
    case AudioSampleFormat::Unknown:
        break;
    }
    return output;
}

} // namespace

struct RuntimePlayer::Impl {
    Impl(RuntimePlayer& owner, RuntimePlayerConfig initialConfig, RuntimePlayerDependencies initialDependencies)
        : owner(owner)
        , config(initialConfig)
        , dependencies(initialDependencies)
        , audioQueue(config.audioQueueCapacity)
        , videoQueue(config.videoQueueCapacity)
        , scheduler(toAvSyncConfig(config.syncConfig))
        , clockTicker(config.syncConfig.positionTickInterval, [this]() {
            publishPlaybackClockTick();
        })
    {
    }

    ~Impl()
    {
        stop();
        if (dependencies.videoPresenter)
            dependencies.videoPresenter->setEvents(nullptr);
    }

    Result<void> open()
    {
        if (!dependencies.audioOutput || !dependencies.videoPresenter)
            return dependencyError("RuntimePlayer requires audio output and video presenter");
        if (!isPlaybackRateSupported(config.playbackRate)) {
            return Result<void>::failure({
                .code = MediaErrorCode::InvalidArgument,
                .message = "RuntimePlayer requires a playback rate between 0.5 and 2.0",
                .detail = {},
            });
        }
        if (tempoProcessingRequired() && !dependencies.audioTempoProcessor) {
            return Result<void>::failure({
                .code = MediaErrorCode::UnsupportedFormat,
                .message = "RuntimePlayer requires an audio tempo processor for non-1x audio",
                .detail = {},
            });
        }

        stop();
        activePlaybackRate.store(config.playbackRate, std::memory_order_release);

        bool tempoConfigured = false;
        if (dependencies.audioTempoProcessor) {
            dependencies.audioTempoProcessor->reset();
            if (tempoProcessingRequired()) {
                auto tempoResult = dependencies.audioTempoProcessor->configure(
                    config.audioFormat,
                    currentPlaybackRate());
                if (!tempoResult.ok())
                    return tempoResult;
                tempoConfigured = true;
            }
        }

        auto audioOpen = dependencies.audioOutput->open(config.audioFormat);
        if (!audioOpen.ok()) {
            if (dependencies.audioTempoProcessor)
                dependencies.audioTempoProcessor->reset();
            return audioOpen;
        }
        auto audioResume = dependencies.audioOutput->resume();
        if (!audioResume.ok()) {
            dependencies.audioOutput->close();
            if (dependencies.audioTempoProcessor)
                dependencies.audioTempoProcessor->reset();
            return audioResume;
        }

        {
            std::lock_guard lock(m_mutex);
            diagnostics = {};
            diagnostics.playbackRate = currentPlaybackRate();
            diagnostics.audioTempoConfigureCount = tempoConfigured ? 1 : 0;
            sessionId = ++lastSessionId;
            generation = 1;
            tempoGeneration = tempoConfigured ? generation : 0;
            running = true;
            paused = false;
            fallbackPending = false;
            pausedSeekPrerollPending = false;
            pausedSeekPrerollReserved = false;
            audioEofSeen = false;
            videoEofSeen = false;
            eofNotificationSent = false;
            audioProcessingFailedGeneration = 0;
            seekClockAnchor.clear();

            audioQueue.reset(sessionId, generation);
            videoQueue.reset(sessionId, generation);
            scheduler.reset(generation, currentPlaybackRate());
            presentTracker.reset(sessionId, generation);
            presentTracker.setMaxPending(dependencies.videoPresenter->capabilities().maxPendingFrames);
            fallbackController.reset(sessionId, generation, config.outputPolicy);
        }

        dependencies.videoPresenter->setEvents(&owner);
        audioThread = std::thread([this]() { audioLoop(); });
        videoThread = std::thread([this]() { videoLoop(); });
        clockTicker.start();
        return Result<void>::success();
    }

    RuntimeFramePushResult enqueueAudio(RuntimeAudioFrame frame)
    {
        if (!isRunning())
            return { .status = RuntimeFramePushStatus::Closed };

        const auto pushResult = audioQueue.push(std::move(frame));
        if (pushResult.status != RuntimeFramePushStatus::Accepted
            && pushResult.status != RuntimeFramePushStatus::Backpressured) {
            return pushResult;
        }

        const auto highWatermark = static_cast<std::uint64_t>(audioQueue.highWatermark());
        {
            std::lock_guard lock(m_mutex);
            ++diagnostics.audioQueued;
            if (pushResult.status == RuntimeFramePushStatus::Backpressured) {
                ++diagnostics.audioBackpressureCount;
                diagnostics.decodeFramePushWaitUs += static_cast<std::uint64_t>(pushResult.waitTime.count());
            }
            diagnostics.audioQueueHighWatermark = std::max<std::uint64_t>(
                diagnostics.audioQueueHighWatermark,
                highWatermark);
        }
        return pushResult;
    }

    RuntimeFramePushResult enqueueVideo(RuntimeVideoFrame frame)
    {
        const auto gateStatus = videoPushGate(frame.sessionId, frame.generation);
        if (gateStatus != RuntimeFramePushStatus::Accepted)
            return { .status = gateStatus };

        const bool nativeFrame = frame.frame.pixelFormat() == PixelFormat::Native;
        const auto pushResult = videoQueue.push(std::move(frame));
        if (pushResult.status != RuntimeFramePushStatus::Accepted
            && pushResult.status != RuntimeFramePushStatus::Backpressured) {
            return pushResult;
        }

        const auto highWatermark = static_cast<std::uint64_t>(videoQueue.highWatermark());
        {
            std::lock_guard lock(m_mutex);
            ++diagnostics.videoQueued;
            if (pushResult.status == RuntimeFramePushStatus::Backpressured) {
                ++diagnostics.videoBackpressureCount;
                diagnostics.decodeFramePushWaitUs += static_cast<std::uint64_t>(pushResult.waitTime.count());
            }
            diagnostics.videoQueueHighWatermark = std::max<std::uint64_t>(
                diagnostics.videoQueueHighWatermark,
                highWatermark);
            if (nativeFrame)
                ++diagnostics.nativeAccepted;
        }
        return pushResult;
    }

    void enqueueEndOfStream(SessionId eofSessionId, Generation eofGeneration)
    {
        if (!isCurrent(eofSessionId, eofGeneration))
            return;

        const auto audioResult = audioQueue.pushEndOfStream(eofSessionId, eofGeneration);
        const auto videoResult = videoQueue.pushEndOfStream(eofSessionId, eofGeneration);
        const auto audioAccepted = audioResult.status == RuntimeFramePushStatus::Accepted
            || audioResult.status == RuntimeFramePushStatus::Backpressured;
        const auto videoAccepted = videoResult.status == RuntimeFramePushStatus::Accepted
            || videoResult.status == RuntimeFramePushStatus::Backpressured;

        if (!audioAccepted || !videoAccepted)
            return;

        const auto audioHighWatermark = static_cast<std::uint64_t>(audioQueue.highWatermark());
        const auto videoHighWatermark = static_cast<std::uint64_t>(videoQueue.highWatermark());
        std::lock_guard lock(m_mutex);
        if (audioResult.status == RuntimeFramePushStatus::Backpressured) {
            ++diagnostics.audioBackpressureCount;
            diagnostics.decodeFramePushWaitUs += static_cast<std::uint64_t>(audioResult.waitTime.count());
        }
        if (videoResult.status == RuntimeFramePushStatus::Backpressured) {
            ++diagnostics.videoBackpressureCount;
            diagnostics.decodeFramePushWaitUs += static_cast<std::uint64_t>(videoResult.waitTime.count());
        }
        diagnostics.audioQueueHighWatermark = std::max(
            diagnostics.audioQueueHighWatermark,
            audioHighWatermark);
        diagnostics.videoQueueHighWatermark = std::max(
            diagnostics.videoQueueHighWatermark,
            videoHighWatermark);
        ++diagnostics.eofAccepted;
    }

    void pause()
    {
        bool shouldPause = false;
        {
            std::lock_guard lock(m_mutex);
            if (!running || paused)
                return;
            paused = true;
            pauseSeekGapClockLocked();
            scheduler.pause();
            shouldPause = true;
        }

        if (shouldPause)
            dependencies.audioOutput->pause();
        m_controlChanged.notify_all();
    }

    void resume()
    {
        bool shouldResume = false;
        {
            std::lock_guard lock(m_mutex);
            if (!running || !paused || fallbackPending
                || audioProcessingFailedGeneration == generation)
                return;
            shouldResume = true;
        }

        if (shouldResume) {
            auto resumeResult = dependencies.audioOutput->resume();
            std::lock_guard lock(m_mutex);
            if (resumeResult.ok()) {
                if (running) {
                    paused = false;
                    pausedSeekPrerollPending = false;
                    pausedSeekPrerollReserved = false;
                    resumeSeekGapClockLocked();
                    scheduler.resume();
                }
            } else {
                if (running) {
                    paused = true;
                    pauseSeekGapClockLocked();
                    scheduler.pause();
                }
            }
        }
        m_controlChanged.notify_all();
        m_presentCapacityChanged.notify_all();
    }

    void setAudioControls(RuntimeAudioControls controls)
    {
        const auto sanitized = sanitizeAudioControls(controls);
        audioVolume.store(sanitized.volume, std::memory_order_relaxed);
        audioMuted.store(sanitized.muted, std::memory_order_relaxed);
    }

    void completeSeek(SessionId completedSessionId, Generation completedGeneration)
    {
        bool shouldResume = false;
        {
            std::lock_guard lock(m_mutex);
            if (!running ||
                !fallbackController.completeSeek(completedSessionId, completedGeneration)) {
                return;
            }

            fallbackPending = false;
            pausedSeekPrerollPending = false;
            pausedSeekPrerollReserved = false;
            scheduler.reset(completedGeneration, currentPlaybackRate());
            shouldResume = true;
        }

        if (shouldResume) {
            auto resumeResult = dependencies.audioOutput->resume();
            std::lock_guard lock(m_mutex);
            if (resumeResult.ok()) {
                if (running && isCurrentLocked(completedSessionId, completedGeneration)) {
                    paused = false;
                    resumeSeekGapClockLocked();
                    scheduler.resume();
                }
            } else {
                if (running && isCurrentLocked(completedSessionId, completedGeneration)) {
                    paused = true;
                    pauseSeekGapClockLocked();
                    scheduler.pause();
                }
            }
        }
        m_controlChanged.notify_all();
    }

    void seek(std::chrono::microseconds position)
    {
        const auto result = beginTimelineDiscontinuity(position, std::nullopt);
        if (!result.ok())
            reportRuntimeError(result.error());
    }

    Result<void> setPlaybackRate(double playbackRate, std::chrono::microseconds position)
    {
        if (position < std::chrono::microseconds::zero()) {
            return Result<void>::failure({
                .code = MediaErrorCode::InvalidArgument,
                .message = "RuntimePlayer requires a non-negative rate-change position",
                .detail = {},
            });
        }
        if (!isPlaybackRateSupported(playbackRate)) {
            return Result<void>::failure({
                .code = MediaErrorCode::InvalidArgument,
                .message = "RuntimePlayer requires a playback rate between 0.5 and 2.0",
                .detail = {},
            });
        }
        if (config.audioClockEnabled
            && !playbackRatesEqual(playbackRate, kDefaultPlaybackRate)
            && !dependencies.audioTempoProcessor) {
            return Result<void>::failure({
                .code = MediaErrorCode::UnsupportedFormat,
                .message = "RuntimePlayer requires an audio tempo processor for non-1x audio",
                .detail = {},
            });
        }
        if (playbackRatesEqual(playbackRate, currentPlaybackRate()))
            return Result<void>::success();

        {
            std::lock_guard lock(m_mutex);
            if (!running) {
                config.playbackRate = playbackRate;
                activePlaybackRate.store(playbackRate, std::memory_order_release);
                return Result<void>::success();
            }
        }
        return beginTimelineDiscontinuity(position, playbackRate);
    }

    Result<void> beginTimelineDiscontinuity(
        std::chrono::microseconds position,
        std::optional<double> newPlaybackRate)
    {
        SessionId activeSession = 0;
        Generation nextGeneration = 0;
        const auto targetPlaybackRate = newPlaybackRate.value_or(currentPlaybackRate());
        {
            std::lock_guard lock(m_mutex);
            if (!running) {
                return Result<void>::failure({
                    .code = MediaErrorCode::InternalStateError,
                    .message = "RuntimePlayer is not open",
                    .detail = {},
                });
            }

            activeSession = sessionId;
            nextGeneration = ++generation;
            fallbackPending = false;
            pausedSeekPrerollPending = paused;
            pausedSeekPrerollReserved = false;
            audioEofSeen = false;
            videoEofSeen = false;
            eofNotificationSent = false;
            audioProcessingFailedGeneration = 0;
            diagnostics.queueAbortCount += 2;
            presentTracker.clear();
            presentTracker.reset(activeSession, nextGeneration);
            presentTracker.setMaxPending(dependencies.videoPresenter->capabilities().maxPendingFrames);
            scheduler.reset(nextGeneration, targetPlaybackRate);
            if (paused)
                scheduler.pause();
            fallbackController.reset(activeSession, nextGeneration, config.outputPolicy);
            seekClockAnchor.begin(nextGeneration, position, config.maxSeekAudioGapFill);
            seekGapClock = {};
            if (newPlaybackRate.has_value()) {
                config.playbackRate = targetPlaybackRate;
                diagnostics.playbackRate = targetPlaybackRate;
                ++diagnostics.playbackRateChangeCount;
            }
        }

        audioQueue.abort();
        videoQueue.abort();
        audioQueue.reset(activeSession, nextGeneration);
        videoQueue.reset(activeSession, nextGeneration);
        // seek 期间保留上一帧，等目标侧新帧提交后自然替换，避免精确预滚或异常 EOF 阶段黑屏。
        const auto flushResult = dependencies.audioOutput->flush();
        if (newPlaybackRate.has_value())
            activePlaybackRate.store(targetPlaybackRate, std::memory_order_release);
        m_controlChanged.notify_all();
        m_presentCapacityChanged.notify_all();
        return flushResult;
    }

    void stop()
    {
        bool shouldStop = false;
        {
            std::lock_guard lock(m_mutex);
            if (!running)
                return;

            running = false;
            paused = false;
            fallbackPending = false;
            pausedSeekPrerollPending = false;
            pausedSeekPrerollReserved = false;
            seekClockAnchor.clear();
            seekGapClock = {};
            audioProcessingFailedGeneration = 0;
            generation = 0;
            diagnostics.queueAbortCount += 2;
            shouldStop = true;
        }

        if (!shouldStop)
            return;

        audioQueue.abort();
        videoQueue.abort();
        m_controlChanged.notify_all();
        m_presentCapacityChanged.notify_all();
        clockTicker.stop();

        dependencies.audioOutput->pause();
        (void)dependencies.audioOutput->flush();
        joinWorkers();

        if (dependencies.audioTempoProcessor)
            dependencies.audioTempoProcessor->reset();
        tempoGeneration = 0;

        dependencies.audioOutput->close();
        dependencies.videoPresenter->clear();

        std::lock_guard lock(m_mutex);
        seekClockAnchor.clear();
        seekGapClock = {};
        presentTracker.clear();
        audioEofSeen = false;
        videoEofSeen = false;
        eofNotificationSent = false;
    }

    RuntimeDiagnostics snapshotDiagnostics() const
    {
        std::lock_guard lock(m_mutex);
        return diagnostics;
    }

    RuntimeTimeline snapshotTimeline() const
    {
        std::lock_guard lock(m_mutex);
        return {
            .sessionId = running ? sessionId : 0,
            .generation = running ? generation : 0,
        };
    }

    ClockSnapshot snapshotClock() const
    {
        Generation activeGeneration = 0;
        bool active = false;
        {
            std::lock_guard lock(m_mutex);
            active = running;
            activeGeneration = generation;
        }
        if (!active || !dependencies.audioOutput)
            return {};

        return effectivePlaybackClock(activeGeneration);
    }

    void onPresentComplete(PresentCompletion completion)
    {
        bool failed = false;
        bool accepted = false;
        PresentStatus failureStatus = completion.status;
        const auto completionDiagnostics = completion.diagnostics;
        {
            std::lock_guard lock(m_mutex);
            if (!running)
                return;

            const auto action = presentTracker.complete(sessionId, generation, std::move(completion));
            accepted = action == PresentCompletionAction::AcceptedSuccess
                || action == PresentCompletionAction::AcceptedFailure;
            failed = action == PresentCompletionAction::AcceptedFailure;
            if (accepted)
                accumulatePresentDiagnostics(completionDiagnostics);
        }

        if (accepted)
            m_presentCapacityChanged.notify_all();
        if (failed)
            handlePresentFailure(failureStatus);
    }

    void audioLoop()
    {
        while (true) {
            RuntimeAudioFrame queuedFrame;
            const auto pop = audioQueue.waitPop(queuedFrame);
            if (pop == RuntimeFrameQueue<RuntimeAudioFrame>::PopResult::Aborted) {
                if (!isRunning())
                    return;
                continue;
            }
            if (pop == RuntimeFrameQueue<RuntimeAudioFrame>::PopResult::Closed)
                return;
            if (pop == RuntimeFrameQueue<RuntimeAudioFrame>::PopResult::EndOfStream) {
                handleAudioEndOfStream(queuedFrame.sessionId, queuedFrame.generation);
                continue;
            }
            if (!isCurrent(queuedFrame.sessionId, queuedFrame.generation))
                continue;

            const auto samples = queuedFrame.frame.samples();
            const auto framePts = queuedFrame.frame.pts();
            const auto frameGeneration = queuedFrame.generation;
            const auto gapDecision = takeSeekAudioGapDecision(
                queuedFrame.sessionId,
                frameGeneration,
                framePts);
            if (gapDecision.shouldFill && !writeSeekGapSilence(queuedFrame.sessionId, gapDecision))
                continue;
            if (gapDecision.exceedsMaxGap
                && !waitUntilSeekGapClockReachesAudio(queuedFrame.sessionId, gapDecision)) {
                continue;
            }

            if (!tempoProcessingRequired()) {
                (void)writeAudioSamples(
                    samples,
                    framePts,
                    queuedFrame.sessionId,
                    frameGeneration,
                    false);
                continue;
            }

            auto prepareResult = prepareTempoProcessor(frameGeneration);
            if (!prepareResult.ok()) {
                handleAudioProcessingFailure(
                    queuedFrame.sessionId,
                    frameGeneration,
                    prepareResult.error());
                continue;
            }

            {
                std::lock_guard lock(m_mutex);
                diagnostics.audioTempoInputSamples += audioSampleFrames(samples);
            }
            auto processResult = dependencies.audioTempoProcessor->process({
                .bytes = samples,
                .pts = framePts,
                .generation = frameGeneration,
                .playbackRate = currentPlaybackRate(),
            });
            if (!processResult.ok()) {
                handleAudioProcessingFailure(
                    queuedFrame.sessionId,
                    frameGeneration,
                    processResult.error());
                continue;
            }
            (void)writeTempoOutput(
                std::move(processResult.value()),
                queuedFrame.sessionId,
                frameGeneration);
        }
    }

    bool tempoProcessingRequired() const
    {
        return config.audioClockEnabled
            && !playbackRatesEqual(currentPlaybackRate(), kDefaultPlaybackRate);
    }

    double currentPlaybackRate() const
    {
        return activePlaybackRate.load(std::memory_order_acquire);
    }

    std::uint64_t audioSampleFrames(std::span<const std::byte> samples) const
    {
        const auto bytesPerFrame = bytesPerAudioSample(config.audioFormat.sampleFormat)
            * static_cast<std::size_t>(std::max(config.audioFormat.channels, 0));
        if (bytesPerFrame == 0)
            return 0;
        return static_cast<std::uint64_t>(samples.size() / bytesPerFrame);
    }

    Result<void> prepareTempoProcessor(Generation targetGeneration)
    {
        if (!tempoProcessingRequired())
            return Result<void>::success();
        if (!dependencies.audioTempoProcessor) {
            return Result<void>::failure({
                .code = MediaErrorCode::UnsupportedFormat,
                .message = "Audio tempo processor is unavailable",
                .detail = {},
            });
        }
        if (tempoGeneration == targetGeneration)
            return Result<void>::success();

        dependencies.audioTempoProcessor->reset();
        {
            std::lock_guard lock(m_mutex);
            ++diagnostics.audioTempoResetCount;
        }
        auto result = dependencies.audioTempoProcessor->configure(
            config.audioFormat,
            currentPlaybackRate());
        if (result.ok()) {
            tempoGeneration = targetGeneration;
            std::lock_guard lock(m_mutex);
            ++diagnostics.audioTempoConfigureCount;
        }
        return result;
    }

    bool writeTempoOutput(AudioTempoOutput output,
                          SessionId checkedSessionId,
                          Generation checkedGeneration)
    {
        for (const auto& buffer : output.buffers) {
            if (!writeAudioSamples(
                    buffer.bytes,
                    buffer.pts,
                    checkedSessionId,
                    checkedGeneration,
                    true)) {
                return false;
            }
        }
        return true;
    }

    bool writeAudioSamples(std::span<const std::byte> samples,
                           std::chrono::microseconds pts,
                           SessionId checkedSessionId,
                           Generation checkedGeneration,
                           bool tempoOutput)
    {
        if (!isCurrent(checkedSessionId, checkedGeneration))
            return false;

        const auto controls = currentAudioControls();
        std::vector<std::byte> controlledSamples;
        std::span<const std::byte> outputSamples = samples;
        if (controls.muted || controls.volume != 1.0f) {
            controlledSamples = applyAudioControls(
                samples,
                controls,
                config.audioFormat.sampleFormat);
            outputSamples = controlledSamples;
        }

        const auto writeResult = dependencies.audioOutput->write({
            .bytes = outputSamples,
            .pts = pts,
            .generation = checkedGeneration,
            .playbackRate = currentPlaybackRate(),
        });
        if (!writeResult.ok()) {
            if (isCurrent(checkedSessionId, checkedGeneration))
                reportRuntimeError(writeResult.error());
            return false;
        }

        std::lock_guard lock(m_mutex);
        if (!isCurrentLocked(checkedSessionId, checkedGeneration))
            return false;
        if (seekGapClock.active
            && seekGapClock.sessionId == checkedSessionId
            && seekGapClock.generation == checkedGeneration) {
            seekGapClock = {};
        }
        ++diagnostics.audioWritten;
        if (tempoOutput)
            diagnostics.audioTempoOutputSamples += audioSampleFrames(outputSamples);
        return true;
    }

    void handleAudioEndOfStream(SessionId checkedSessionId, Generation checkedGeneration)
    {
        if (!isCurrent(checkedSessionId, checkedGeneration))
            return;

        if (tempoProcessingRequired()) {
            auto prepareResult = prepareTempoProcessor(checkedGeneration);
            if (!prepareResult.ok()) {
                handleAudioProcessingFailure(
                    checkedSessionId,
                    checkedGeneration,
                    prepareResult.error());
                return;
            }

            auto drainResult = dependencies.audioTempoProcessor->drain();
            {
                std::lock_guard lock(m_mutex);
                ++diagnostics.audioTempoDrainCount;
            }
            if (!drainResult.ok()) {
                handleAudioProcessingFailure(
                    checkedSessionId,
                    checkedGeneration,
                    drainResult.error());
                return;
            }
            if (!writeTempoOutput(
                    std::move(drainResult.value()),
                    checkedSessionId,
                    checkedGeneration)) {
                return;
            }
        }

        if (waitForAudioOutputDrain(checkedSessionId, checkedGeneration))
            markEof(true, checkedSessionId, checkedGeneration);
    }

    bool waitForAudioOutputDrain(SessionId checkedSessionId, Generation checkedGeneration)
    {
        while (isCurrent(checkedSessionId, checkedGeneration)) {
            const auto snapshot = dependencies.audioOutput->clock();
            if (!snapshot.valid || snapshot.generation != checkedGeneration
                || snapshot.queuedDuration <= std::chrono::microseconds::zero()) {
                return true;
            }

            std::unique_lock lock(m_mutex);
            if (!isCurrentLocked(checkedSessionId, checkedGeneration))
                return false;
            if (paused) {
                m_controlChanged.wait(lock, [this, checkedSessionId, checkedGeneration]() {
                    return !isCurrentLocked(checkedSessionId, checkedGeneration) || !paused;
                });
                continue;
            }
            m_controlChanged.wait_for(lock, std::chrono::milliseconds(2));
        }
        return false;
    }

    void handleAudioProcessingFailure(SessionId checkedSessionId,
                                      Generation checkedGeneration,
                                      MediaError error)
    {
        bool accepted = false;
        {
            std::lock_guard lock(m_mutex);
            if (isCurrentLocked(checkedSessionId, checkedGeneration)) {
                ++diagnostics.audioTempoFailureCount;
                audioProcessingFailedGeneration = checkedGeneration;
                paused = true;
                pauseSeekGapClockLocked();
                scheduler.pause();
                accepted = true;
            }
        }
        if (!accepted)
            return;

        dependencies.audioOutput->pause();
        reportRuntimeError(std::move(error));
        m_controlChanged.notify_all();
        m_presentCapacityChanged.notify_all();

        std::unique_lock lock(m_mutex);
        m_controlChanged.wait(lock, [this, checkedSessionId, checkedGeneration]() {
            return !running
                || !isCurrentLocked(checkedSessionId, checkedGeneration)
                || audioProcessingFailedGeneration != checkedGeneration;
        });
    }

    SeekAudioGapDecision takeSeekAudioGapDecision(
        SessionId checkedSessionId,
        Generation checkedGeneration,
        std::chrono::microseconds firstAudioPts)
    {
        std::lock_guard lock(m_mutex);
        if (!isCurrentLocked(checkedSessionId, checkedGeneration))
            return {};
        return seekClockAnchor.inspectFirstAudio(checkedGeneration, firstAudioPts);
    }

    bool waitUntilSeekGapClockReachesAudio(
        SessionId checkedSessionId,
        SeekAudioGapDecision decision)
    {
        {
            std::lock_guard lock(m_mutex);
            if (!isCurrentLocked(checkedSessionId, decision.generation))
                return false;
            // 大 gap 不写超长静音；使用自驱动时钟保持 position/video 调度连续，
            // 到达首个真实音频 PTS 后再切回底层音频时钟。
            seekGapClock = {
                .active = true,
                .paused = paused,
                .sessionId = checkedSessionId,
                .generation = decision.generation,
                .basePosition = decision.target,
                .targetAudioPts = decision.firstAudioPts,
                .baseTime = SteadyClock::now(),
                .playbackRate = currentPlaybackRate(),
            };
        }
        m_controlChanged.notify_all();

        while (true) {
            std::unique_lock lock(m_mutex);
            if (!running || !isCurrentLocked(checkedSessionId, decision.generation))
                return false;
            if (!seekGapClock.active
                || seekGapClock.sessionId != checkedSessionId
                || seekGapClock.generation != decision.generation) {
                return false;
            }
            if (seekGapClock.paused) {
                m_controlChanged.wait(lock, [this, checkedSessionId, generation = decision.generation]() {
                    return !running || !isCurrentLocked(checkedSessionId, generation)
                        || !seekGapClock.active || !seekGapClock.paused;
                });
                continue;
            }

            const auto position = seekGapClockPositionLocked(SteadyClock::now());
            if (position >= seekGapClock.targetAudioPts)
                return true;

            const auto waitTime = std::min<std::chrono::microseconds>(
                playbackDurationForMediaDuration(
                    seekGapClock.targetAudioPts - position,
                    seekGapClock.playbackRate),
                std::chrono::milliseconds(20));
            m_controlChanged.wait_for(lock, waitTime);
        }
    }

    bool writeSeekGapSilence(SessionId checkedSessionId, SeekAudioGapDecision decision)
    {
        constexpr auto kSilenceDeviceChunkDuration = std::chrono::milliseconds(20);
        const auto playbackRate = currentPlaybackRate();
        auto remaining = decision.gap;
        auto chunkPts = decision.target;
        const auto maxMediaChunkDuration = mediaDurationForPlaybackDuration(
            kSilenceDeviceChunkDuration,
            playbackRate);

        while (remaining > std::chrono::microseconds::zero()) {
            if (!waitUntilCanWriteSeekGapSilence(checkedSessionId, decision.generation))
                return false;

            const auto mediaChunkDuration = std::min<std::chrono::microseconds>(
                remaining,
                maxMediaChunkDuration);
            const auto deviceChunkDuration = playbackDurationForMediaDuration(
                mediaChunkDuration,
                playbackRate);
            auto silence = makeSilenceBytes(config.audioFormat, deviceChunkDuration);
            if (!silence.ok()) {
                reportRuntimeError(silence.error());
                return true;
            }
            if (silence.value().empty())
                return true;

            // seek 后首个真实音频 PTS 可能晚于目标点。先写入目标时间戳静音，
            // 让底层音频时钟从目标点启动，再自然推进到首个真实音频帧。
            const auto writeResult = dependencies.audioOutput->write({
                .bytes = silence.value(),
                .pts = chunkPts,
                .generation = decision.generation,
                .playbackRate = playbackRate,
            });
            if (!writeResult.ok()) {
                if (isCurrent(checkedSessionId, decision.generation))
                    reportRuntimeError(writeResult.error());
                return false;
            }

            remaining -= mediaChunkDuration;
            chunkPts += mediaChunkDuration;
        }
        return true;
    }

    bool waitUntilCanWriteSeekGapSilence(SessionId checkedSessionId, Generation checkedGeneration)
    {
        std::unique_lock lock(m_mutex);
        m_controlChanged.wait(lock, [this, checkedSessionId, checkedGeneration]() {
            return !running || !isCurrentLocked(checkedSessionId, checkedGeneration) || !paused;
        });
        return isCurrentLocked(checkedSessionId, checkedGeneration) && !paused;
    }

    ClockSnapshot audioClockSnapshotForGeneration(Generation activeGeneration) const
    {
        auto snapshot = dependencies.audioOutput->clock();
        if (snapshot.generation != activeGeneration)
            snapshot.valid = false;
        return snapshot;
    }

    ClockSnapshot syntheticPlaybackClockLocked(
        Generation activeGeneration,
        ClockSnapshot snapshot,
        std::chrono::microseconds position) const
    {
        return {
            .position = position,
            .hardwareLatency = snapshot.hardwareLatency,
            .queuedDuration = std::chrono::microseconds { 0 },
            .generation = activeGeneration,
            .valid = true,
            .paused = paused,
        };
    }

    ClockSnapshot seekGapPlaybackClockLocked(
        Generation activeGeneration,
        ClockSnapshot snapshot) const
    {
        const auto position = seekGapClockPositionLocked(SteadyClock::now());
        if (snapshot.valid && snapshot.position >= position)
            return snapshot;
        return syntheticPlaybackClockLocked(activeGeneration, snapshot, position);
    }

    ClockSnapshot seekAnchorPlaybackClockLocked(
        Generation activeGeneration,
        ClockSnapshot snapshot) const
    {
        const auto target = seekClockAnchor.target();
        if (snapshot.valid && snapshot.position >= target)
            return snapshot;
        return syntheticPlaybackClockLocked(activeGeneration, snapshot, target);
    }

    ClockSnapshot effectivePlaybackClock(Generation activeGeneration) const
    {
        if (!config.audioClockEnabled) {
            std::lock_guard lock(m_mutex);
            if (!running || generation != activeGeneration)
                return {};
            return scheduler.clockSnapshot(activeGeneration);
        }

        const auto snapshot = audioClockSnapshotForGeneration(activeGeneration);

        ClockSnapshot result;
        bool seekGapActive = false;
        bool seekAnchorActive = false;
        {
            std::lock_guard lock(m_mutex);
            seekGapActive = seekGapClock.active && seekGapClock.generation == activeGeneration;
            seekAnchorActive = config.audioClockEnabled && seekClockAnchor.activeFor(activeGeneration);
            // 时钟优先级：大 gap 自驱动时钟 > 首音频前 seek target 锚点 > 底层音频时钟。
            if (!running)
                result = snapshot;
            else if (seekGapActive)
                result = seekGapPlaybackClockLocked(activeGeneration, snapshot);
            else if (seekAnchorActive)
                result = seekAnchorPlaybackClockLocked(activeGeneration, snapshot);
            else
                result = snapshot;
            if (running)
                result.paused = paused;
        }
        return result;
    }

    std::chrono::microseconds seekGapClockPositionLocked(
        SteadyClock::time_point now) const
    {
        if (!seekGapClock.active)
            return std::chrono::microseconds { 0 };
        if (seekGapClock.paused)
            return seekGapClock.basePosition;
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            now - seekGapClock.baseTime);
        return std::min(
            seekGapClock.targetAudioPts,
            seekGapClock.basePosition
                + mediaDurationForPlaybackDuration(elapsed, seekGapClock.playbackRate));
    }

    void pauseSeekGapClockLocked()
    {
        if (!seekGapClock.active || seekGapClock.paused)
            return;
        seekGapClock.basePosition = seekGapClockPositionLocked(SteadyClock::now());
        seekGapClock.baseTime = {};
        seekGapClock.paused = true;
    }

    void resumeSeekGapClockLocked()
    {
        if (!seekGapClock.active || !seekGapClock.paused)
            return;
        seekGapClock.baseTime = SteadyClock::now();
        seekGapClock.paused = false;
    }

    RuntimeAudioControls currentAudioControls() const
    {
        return {
            .volume = audioVolume.load(std::memory_order_relaxed),
            .muted = audioMuted.load(std::memory_order_relaxed),
        };
    }

    void videoLoop()
    {
        while (true) {
            RuntimeVideoFrame queuedFrame;
            const auto pop = videoQueue.waitPop(queuedFrame);
            if (pop == RuntimeFrameQueue<RuntimeVideoFrame>::PopResult::Aborted) {
                if (!isRunning())
                    return;
                continue;
            }
            if (pop == RuntimeFrameQueue<RuntimeVideoFrame>::PopResult::Closed)
                return;
            if (pop == RuntimeFrameQueue<RuntimeVideoFrame>::PopResult::EndOfStream) {
                markEof(false, queuedFrame.sessionId, queuedFrame.generation);
                continue;
            }
            processVideoFrame(std::move(queuedFrame));
        }
    }

    void processVideoFrame(RuntimeVideoFrame queuedFrame)
    {
        if (!isCurrent(queuedFrame.sessionId, queuedFrame.generation))
            return;
        const bool pausedPreroll = isPausedSeekPreroll(queuedFrame.sessionId, queuedFrame.generation);
        if (!pausedPreroll && !waitUntilUnpaused(queuedFrame.sessionId, queuedFrame.generation))
            return;

        while (true) {
            auto clock = config.audioClockEnabled
                ? effectivePlaybackClock(queuedFrame.generation)
                : ClockSnapshot {};
            const auto decision = pausedPreroll
                ? VideoScheduleDecision {
                    .action = VideoScheduleAction::Render,
                    .lateness = std::chrono::microseconds { 0 },
                    .waitTime = std::chrono::microseconds { 0 },
                }
                : decideFrame(queuedFrame.frame.pts(), clock, queuedFrame.generation);
            if (decision.action == VideoScheduleAction::Drop) {
                std::lock_guard lock(m_mutex);
                ++diagnostics.videoDroppedLate;
                return;
            }

            if (decision.action == VideoScheduleAction::Wait) {
                {
                    std::lock_guard lock(m_mutex);
                    ++diagnostics.videoWaited;
                }
                if (!waitForFrameTime(decision.waitTime, queuedFrame.sessionId, queuedFrame.generation))
                    return;
                continue;
            }

            const auto clockPosition = clock.valid && clock.generation == queuedFrame.generation
                ? clock.position
                : queuedFrame.frame.pts();
            PresentTiming timing {
                .pts = queuedFrame.frame.pts(),
                .clock = clockPosition,
                .lateness = decision.lateness,
            };
            if (!waitForPresentCapacity(queuedFrame.sessionId, queuedFrame.generation))
                return;

            const auto frameSessionId = queuedFrame.sessionId;
            const auto frameGeneration = queuedFrame.generation;
            const bool presentedNativeFrame = queuedFrame.frame.pixelFormat() == PixelFormat::Native;
            const auto result = dependencies.videoPresenter->present(std::move(queuedFrame.frame), timing);
            if (isFailureStatus(result.status)) {
                handlePresentFailure(result.status);
                return;
            }

            bool trackFailed = false;
            bool earlyFailure = false;
            PresentStatus earlyFailureStatus = PresentStatus::Failed;
            {
                std::lock_guard lock(m_mutex);
                if (!isCurrentLocked(frameSessionId, frameGeneration))
                    return;

                if (result.status == PresentStatus::Queued) {
                    const auto trackResult = presentTracker.trackResult({
                        .id = result.id,
                        .sessionId = frameSessionId,
                        .generation = frameGeneration,
                        .nativeFrame = presentedNativeFrame,
                    });
                    if (trackResult.action == PresentTrackAction::Rejected) {
                        trackFailed = true;
                    } else if (trackResult.action == PresentTrackAction::ConsumedFailure) {
                        earlyFailure = true;
                        earlyFailureStatus = trackResult.completionStatus;
                        accumulatePresentDiagnostics(trackResult.diagnostics);
                    } else if (trackResult.action == PresentTrackAction::ConsumedSuccess) {
                        accumulatePresentDiagnostics(trackResult.diagnostics);
                    }
                }

                if (!trackFailed) {
                    ++diagnostics.videoPresented;
                    if (presentedNativeFrame)
                        ++diagnostics.nativePresented;
                    else
                        ++diagnostics.cpuPresented;
                    if (pausedPreroll)
                        completePausedSeekPrerollLocked(frameSessionId, frameGeneration);
                }
            }
            if (trackFailed)
                handlePresentFailure(PresentStatus::Failed);
            if (earlyFailure)
                handlePresentFailure(earlyFailureStatus);
            return;
        }
    }

    VideoScheduleDecision decideFrame(
        std::chrono::microseconds pts,
        ClockSnapshot clock,
        Generation frameGeneration)
    {
        std::lock_guard lock(m_mutex);
        return scheduler.decide(pts, clock, frameGeneration);
    }

    bool waitUntilUnpaused(SessionId checkedSessionId, Generation checkedGeneration)
    {
        std::unique_lock lock(m_mutex);
        m_controlChanged.wait(lock, [this, checkedSessionId, checkedGeneration]() {
            return !running || !isCurrentLocked(checkedSessionId, checkedGeneration) || !paused;
        });
        return running && isCurrentLocked(checkedSessionId, checkedGeneration);
    }

    bool waitForFrameTime(
        std::chrono::microseconds waitTime,
        SessionId checkedSessionId,
        Generation checkedGeneration)
    {
        std::unique_lock lock(m_mutex);
        m_controlChanged.wait_for(lock, waitTime, [this, checkedSessionId, checkedGeneration]() {
            return !running || !isCurrentLocked(checkedSessionId, checkedGeneration) || paused;
        });
        while (running && paused && isCurrentLocked(checkedSessionId, checkedGeneration))
            m_controlChanged.wait(lock);
        return running && isCurrentLocked(checkedSessionId, checkedGeneration);
    }

    bool waitForPresentCapacity(SessionId checkedSessionId, Generation checkedGeneration)
    {
        std::unique_lock lock(m_mutex);
        while (running
            && isCurrentLocked(checkedSessionId, checkedGeneration)
            && !presentTracker.hasCapacity()) {
            m_presentCapacityChanged.wait(lock, [this, checkedSessionId, checkedGeneration]() {
                    return !running
                        || !isCurrentLocked(checkedSessionId, checkedGeneration)
                        || presentTracker.hasCapacity();
                });
        }
        return running && isCurrentLocked(checkedSessionId, checkedGeneration);
    }

    bool isPausedSeekPreroll(SessionId checkedSessionId, Generation checkedGeneration) const
    {
        std::lock_guard lock(m_mutex);
        return running
            && isCurrentLocked(checkedSessionId, checkedGeneration)
            && paused
            && pausedSeekPrerollPending;
    }

    void markEof(bool audio, SessionId checkedSessionId, Generation checkedGeneration)
    {
        bool completed = false;
        {
            std::lock_guard lock(m_mutex);
            if (!running || !isCurrentLocked(checkedSessionId, checkedGeneration))
                return;

            if (audio)
                audioEofSeen = true;
            else
                videoEofSeen = true;

            if (audioEofSeen && videoEofSeen && !eofNotificationSent) {
                ++diagnostics.eofPresented;
                completed = true;
                eofNotificationSent = true;
            }
        }
        if (completed && dependencies.events) {
            dependencies.events->onEndOfStreamPresented({
                .sessionId = checkedSessionId,
                .generation = checkedGeneration,
            });
        }
    }

    void accumulatePresentDiagnostics(PresentDiagnostics presentDiagnostics)
    {
        diagnostics.nativeTextureCreated += presentDiagnostics.nativeTextureCreated;
        diagnostics.nativeTextureFailed += presentDiagnostics.nativeTextureFailed;
        diagnostics.nativeTextureDrawn += presentDiagnostics.nativeTextureDrawn;
        diagnostics.cpuCopied += presentDiagnostics.cpuCopied;
        diagnostics.cpuTransferred += presentDiagnostics.cpuTransferred;
        diagnostics.cpuMemcpy += presentDiagnostics.cpuMemcpy;
    }

    void handlePresentFailure(PresentStatus reason)
    {
        const auto clock = snapshotClock();
        FallbackTransition transition;
        SessionId activeSession = 0;
        RuntimeFallbackAction fallbackAction;
        bool requestCpuDecode = false;
        {
            std::lock_guard lock(m_mutex);
            if (!running)
                return;

            transition = fallbackController.beginFallback({
                .sessionId = sessionId,
                .generation = generation,
                .reason = reason,
                .resumePosition = clock.position,
            });
            if (!transition.accepted)
                return;

            activeSession = sessionId;
            ++diagnostics.nativeFallbacks;
            config.outputPolicy = transition.newPolicy;
            generation = transition.newGeneration;
            paused = true;
            fallbackPending = true;
            audioEofSeen = false;
            videoEofSeen = false;
            eofNotificationSent = false;
            audioProcessingFailedGeneration = 0;
            seekClockAnchor.clear();
            seekGapClock = {};
            presentTracker.clear();
            presentTracker.reset(activeSession, generation);
            presentTracker.setMaxPending(dependencies.videoPresenter->capabilities().maxPendingFrames);
            scheduler.reset(generation, currentPlaybackRate());
            scheduler.pause();
            if (transition.abortQueues)
                diagnostics.queueAbortCount += 2;
            requestCpuDecode = transition.requestCpuDecode;
            fallbackAction = {
                .sessionId = activeSession,
                .generation = generation,
                .resumePosition = clock.position,
                .outputPolicy = transition.newPolicy,
                .preferNativeVideoFrames = false,
            };
        }

        if (transition.pauseAudio)
            dependencies.audioOutput->pause();
        if (transition.flushAudio) {
            const auto flushResult = dependencies.audioOutput->flush();
            if (!flushResult.ok())
                reportRuntimeError(flushResult.error());
        }
        if (transition.abortQueues) {
            audioQueue.abort();
            videoQueue.abort();
            audioQueue.reset(activeSession, transition.newGeneration);
            videoQueue.reset(activeSession, transition.newGeneration);
        }
        if (transition.clearPresenter)
            dependencies.videoPresenter->clear();
        if (requestCpuDecode && dependencies.events)
            dependencies.events->onFallbackToCpuRequested(fallbackAction);
        m_controlChanged.notify_all();
        m_presentCapacityChanged.notify_all();
    }

    void notifyPresenterFailure(PresentStatus reason)
    {
        if (!isFailureStatus(reason))
            return;
        handlePresentFailure(reason);
    }

    void reportRuntimeError(MediaError error) const
    {
        if (dependencies.events)
            dependencies.events->onRuntimeError(std::move(error));
    }

    bool isRunning() const
    {
        std::lock_guard lock(m_mutex);
        return running;
    }

    RuntimeFramePushStatus videoPushGate(SessionId checkedSessionId, Generation checkedGeneration)
    {
        std::lock_guard lock(m_mutex);
        if (!running)
            return RuntimeFramePushStatus::Closed;
        if (checkedSessionId != sessionId || checkedGeneration != generation)
            return RuntimeFramePushStatus::RejectedGeneration;
        if (fallbackPending)
            return RuntimeFramePushStatus::Closed;
        if (paused) {
            if (!pausedSeekPrerollPending || pausedSeekPrerollReserved)
                return RuntimeFramePushStatus::Cancelled;
            pausedSeekPrerollReserved = true;
        }
        return RuntimeFramePushStatus::Accepted;
    }

    void completePausedSeekPrerollLocked(SessionId checkedSessionId, Generation checkedGeneration)
    {
        if (!isCurrentLocked(checkedSessionId, checkedGeneration))
            return;
        pausedSeekPrerollPending = false;
        pausedSeekPrerollReserved = false;
    }

    bool isCurrent(SessionId checkedSessionId, Generation checkedGeneration) const
    {
        std::lock_guard lock(m_mutex);
        return isCurrentLocked(checkedSessionId, checkedGeneration);
    }

    bool isCurrentLocked(SessionId checkedSessionId, Generation checkedGeneration) const
    {
        return running && checkedSessionId == sessionId && checkedGeneration == generation;
    }

    void joinWorkers()
    {
        if (audioThread.joinable())
            audioThread.join();
        if (videoThread.joinable())
            videoThread.join();
    }

    void publishPlaybackClockTick()
    {
        SessionId activeSessionId = 0;
        Generation activeGeneration = 0;
        {
            std::lock_guard lock(m_mutex);
            if (!running || paused || generation == 0)
                return;
            activeSessionId = sessionId;
            activeGeneration = generation;
        }

        const auto clock = effectivePlaybackClock(activeGeneration);
        if (!clock.valid || clock.generation != activeGeneration)
            return;

        {
            std::lock_guard lock(m_mutex);
            if (!isCurrentLocked(activeSessionId, activeGeneration) || paused)
                return;
        }

        if (dependencies.events) {
            dependencies.events->onPlaybackClockTick(
                {
                    .sessionId = activeSessionId,
                    .generation = activeGeneration,
                },
                clock);
        }
    }

    RuntimePlayer& owner;
    RuntimePlayerConfig config;
    RuntimePlayerDependencies dependencies;
    RuntimeFrameQueue<RuntimeAudioFrame> audioQueue;
    RuntimeFrameQueue<RuntimeVideoFrame> videoQueue;
    AvSyncScheduler scheduler;
    RuntimeClockTicker clockTicker;
    PresentTracker presentTracker;
    NativeFallbackController fallbackController;
    SeekClockAnchor seekClockAnchor;
    struct SeekGapClock {
        bool active = false;
        bool paused = false;
        SessionId sessionId = 0;
        Generation generation = 0;
        std::chrono::microseconds basePosition { 0 };
        std::chrono::microseconds targetAudioPts { 0 };
        SteadyClock::time_point baseTime {};
        double playbackRate = kDefaultPlaybackRate;
    };
    SeekGapClock seekGapClock;
    std::atomic<float> audioVolume { sanitizeAudioControls(config.audioControls).volume };
    std::atomic_bool audioMuted { sanitizeAudioControls(config.audioControls).muted };
    std::atomic<double> activePlaybackRate { config.playbackRate };
    mutable std::mutex m_mutex;
    std::condition_variable m_controlChanged;
    std::condition_variable m_presentCapacityChanged;
    std::thread audioThread;
    std::thread videoThread;
    RuntimeDiagnostics diagnostics {};
    SessionId lastSessionId = 0;
    SessionId sessionId = 0;
    Generation generation = 0;
    Generation tempoGeneration = 0;
    Generation audioProcessingFailedGeneration = 0;
    bool running = false;
    bool paused = false;
    bool fallbackPending = false;
    bool pausedSeekPrerollPending = false;
    bool pausedSeekPrerollReserved = false;
    bool audioEofSeen = false;
    bool videoEofSeen = false;
    bool eofNotificationSent = false;
};

RuntimePlayer::RuntimePlayer(RuntimePlayerConfig config, RuntimePlayerDependencies dependencies)
    : m_impl(std::make_unique<Impl>(*this, config, dependencies))
{
}

RuntimePlayer::~RuntimePlayer() = default;

Result<void> RuntimePlayer::open()
{
    return m_impl->open();
}

RuntimeFramePushResult RuntimePlayer::enqueueAudio(RuntimeAudioFrame frame)
{
    return m_impl->enqueueAudio(std::move(frame));
}

RuntimeFramePushResult RuntimePlayer::enqueueVideo(RuntimeVideoFrame frame)
{
    return m_impl->enqueueVideo(std::move(frame));
}

void RuntimePlayer::enqueueEndOfStream(SessionId sessionId, Generation generation)
{
    m_impl->enqueueEndOfStream(sessionId, generation);
}

void RuntimePlayer::pause()
{
    m_impl->pause();
}

void RuntimePlayer::resume()
{
    m_impl->resume();
}

void RuntimePlayer::setAudioControls(RuntimeAudioControls controls)
{
    m_impl->setAudioControls(controls);
}

void RuntimePlayer::seek(std::chrono::microseconds position)
{
    m_impl->seek(position);
}

Result<void> RuntimePlayer::setPlaybackRate(
    double playbackRate,
    std::chrono::microseconds position)
{
    return m_impl->setPlaybackRate(playbackRate, position);
}

double RuntimePlayer::playbackRate() const
{
    return m_impl->currentPlaybackRate();
}

void RuntimePlayer::completeSeek(SessionId sessionId, Generation generation)
{
    m_impl->completeSeek(sessionId, generation);
}

void RuntimePlayer::notifyPresenterFailure(PresentStatus reason)
{
    m_impl->notifyPresenterFailure(reason);
}

void RuntimePlayer::stop()
{
    m_impl->stop();
}

RuntimeDiagnostics RuntimePlayer::diagnostics() const
{
    return m_impl->snapshotDiagnostics();
}

ClockSnapshot RuntimePlayer::clock() const
{
    return m_impl->snapshotClock();
}

RuntimeTimeline RuntimePlayer::timeline() const
{
    return m_impl->snapshotTimeline();
}

void RuntimePlayer::onPresentComplete(PresentCompletion completion)
{
    m_impl->onPresentComplete(std::move(completion));
}

} // namespace media_sdk::runtime
