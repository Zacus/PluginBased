#include "media_sdk/runtime/RuntimePlayer.h"

#include "AvSyncScheduler.h"
#include "NativeFallbackController.h"
#include "PresentTracker.h"
#include "RuntimeFrameQueue.h"
#include "media_sdk/Error.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace media_sdk::runtime {
namespace {

AvSyncConfig toAvSyncConfig(RuntimeSyncConfig config)
{
    return {
        .submitLeadTime = config.submitLeadTime,
        .lateDropThreshold = config.lateDropThreshold,
        .maxScheduledWait = config.maxScheduledWait,
        .maxConsecutiveDropsBeforeForceRender = config.maxConsecutiveDropsBeforeForceRender,
    };
}

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

std::vector<std::byte> applyAudioControls(std::span<const std::byte> samples,
                                          RuntimeAudioControls controls)
{
    std::vector<std::byte> output(samples.begin(), samples.end());
    if (output.empty())
        return output;

    if (controls.muted || controls.volume == 0.0f) {
        std::fill(output.begin(), output.end(), std::byte { 0 });
        return output;
    }

    if (output.size() % sizeof(float) != 0)
        return output;

    for (std::size_t offset = 0; offset < output.size(); offset += sizeof(float)) {
        float sample = 0.0f;
        std::memcpy(&sample, output.data() + offset, sizeof(sample));
        sample *= controls.volume;
        std::memcpy(output.data() + offset, &sample, sizeof(sample));
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

        stop();

        auto audioOpen = dependencies.audioOutput->open(config.audioFormat);
        if (!audioOpen.ok())
            return audioOpen;
        auto audioResume = dependencies.audioOutput->resume();
        if (!audioResume.ok()) {
            dependencies.audioOutput->close();
            return audioResume;
        }

        {
            std::lock_guard lock(m_mutex);
            diagnostics = {};
            sessionId = ++lastSessionId;
            generation = 1;
            running = true;
            paused = false;
            fallbackPending = false;
            audioEofSeen = false;
            videoEofSeen = false;
            eofNotificationSent = false;

            audioQueue.reset(sessionId, generation);
            videoQueue.reset(sessionId, generation);
            scheduler.reset(generation);
            presentTracker.reset(sessionId, generation);
            presentTracker.setMaxPending(dependencies.videoPresenter->capabilities().maxPendingFrames);
            fallbackController.reset(sessionId, generation, config.outputPolicy);
        }

        dependencies.videoPresenter->setEvents(&owner);
        audioThread = std::thread([this]() { audioLoop(); });
        videoThread = std::thread([this]() { videoLoop(); });
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
        if (!isAcceptingVideo())
            return { .status = RuntimeFramePushStatus::Closed };

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
            if (!running || !paused || fallbackPending)
                return;
            paused = false;
            shouldResume = true;
        }

        if (shouldResume) {
            auto resumeResult = dependencies.audioOutput->resume();
            if (!resumeResult.ok()) {
                std::lock_guard lock(m_mutex);
                if (running)
                    paused = true;
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
            paused = false;
            scheduler.reset(completedGeneration);
            shouldResume = true;
        }

        if (shouldResume) {
            auto resumeResult = dependencies.audioOutput->resume();
            if (!resumeResult.ok()) {
                std::lock_guard lock(m_mutex);
                if (running && isCurrentLocked(completedSessionId, completedGeneration))
                    paused = true;
            }
        }
        m_controlChanged.notify_all();
    }

    void seek(std::chrono::microseconds)
    {
        SessionId activeSession = 0;
        Generation nextGeneration = 0;
        {
            std::lock_guard lock(m_mutex);
            if (!running)
                return;

            activeSession = sessionId;
            nextGeneration = ++generation;
            fallbackPending = false;
            audioEofSeen = false;
            videoEofSeen = false;
            eofNotificationSent = false;
            diagnostics.queueAbortCount += 2;
            presentTracker.clear();
            presentTracker.reset(activeSession, nextGeneration);
            presentTracker.setMaxPending(dependencies.videoPresenter->capabilities().maxPendingFrames);
            scheduler.reset(nextGeneration);
            fallbackController.reset(activeSession, nextGeneration, config.outputPolicy);
        }

        audioQueue.abort();
        videoQueue.abort();
        audioQueue.reset(activeSession, nextGeneration);
        videoQueue.reset(activeSession, nextGeneration);
        dependencies.videoPresenter->clear();
        dependencies.audioOutput->flush();
        m_controlChanged.notify_all();
        m_presentCapacityChanged.notify_all();
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
        joinWorkers();

        dependencies.audioOutput->pause();
        dependencies.audioOutput->flush();
        dependencies.audioOutput->close();
        dependencies.videoPresenter->clear();

        std::lock_guard lock(m_mutex);
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
                markEof(true, queuedFrame.sessionId, queuedFrame.generation);
                continue;
            }
            if (!isCurrent(queuedFrame.sessionId, queuedFrame.generation))
                continue;

            const auto samples = queuedFrame.frame.samples();
            const auto controls = currentAudioControls();
            std::vector<std::byte> controlledSamples;
            std::span<const std::byte> outputSamples = samples;
            if (controls.muted || controls.volume != 1.0f) {
                controlledSamples = applyAudioControls(samples, controls);
                outputSamples = controlledSamples;
            }
            const auto writeResult = dependencies.audioOutput->write({
                .bytes = outputSamples,
                .pts = queuedFrame.frame.pts(),
                .generation = queuedFrame.generation,
            });
            if (writeResult.ok()) {
                std::lock_guard lock(m_mutex);
                ++diagnostics.audioWritten;
            }
        }
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
        if (!waitUntilUnpaused(queuedFrame.sessionId, queuedFrame.generation))
            return;

        while (true) {
            auto clock = dependencies.audioOutput->clock();
            if (!config.audioClockEnabled)
                clock.valid = false;
            const auto decision = decideFrame(queuedFrame.frame.pts(), clock, queuedFrame.generation);
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
            {
                std::lock_guard lock(m_mutex);
                if (!isCurrentLocked(frameSessionId, frameGeneration))
                    return;

                if (result.status == PresentStatus::Queued) {
                    trackFailed = !presentTracker.track({
                        .id = result.id,
                        .sessionId = frameSessionId,
                        .generation = frameGeneration,
                        .nativeFrame = presentedNativeFrame,
                    });
                }

                if (!trackFailed) {
                    ++diagnostics.videoPresented;
                    if (presentedNativeFrame)
                        ++diagnostics.nativePresented;
                    else
                        ++diagnostics.cpuPresented;
                }
            }
            if (trackFailed)
                handlePresentFailure(PresentStatus::Failed);
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
        m_presentCapacityChanged.wait(lock, [this, checkedSessionId, checkedGeneration]() {
            return !running
                || !isCurrentLocked(checkedSessionId, checkedGeneration)
                || presentTracker.hasCapacity();
        });
        return running && isCurrentLocked(checkedSessionId, checkedGeneration);
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
        const auto clock = dependencies.audioOutput->clock();
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
            presentTracker.clear();
            presentTracker.reset(activeSession, generation);
            presentTracker.setMaxPending(dependencies.videoPresenter->capabilities().maxPendingFrames);
            scheduler.reset(generation);
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
        if (transition.flushAudio)
            dependencies.audioOutput->flush();
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

    bool isRunning() const
    {
        std::lock_guard lock(m_mutex);
        return running;
    }

    bool isAcceptingVideo() const
    {
        std::lock_guard lock(m_mutex);
        return running && !paused && !fallbackPending;
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

    RuntimePlayer& owner;
    RuntimePlayerConfig config;
    RuntimePlayerDependencies dependencies;
    RuntimeFrameQueue<RuntimeAudioFrame> audioQueue;
    RuntimeFrameQueue<RuntimeVideoFrame> videoQueue;
    AvSyncScheduler scheduler;
    PresentTracker presentTracker;
    NativeFallbackController fallbackController;
    std::atomic<float> audioVolume { sanitizeAudioControls(config.audioControls).volume };
    std::atomic_bool audioMuted { sanitizeAudioControls(config.audioControls).muted };
    mutable std::mutex m_mutex;
    std::condition_variable m_controlChanged;
    std::condition_variable m_presentCapacityChanged;
    std::thread audioThread;
    std::thread videoThread;
    RuntimeDiagnostics diagnostics {};
    SessionId lastSessionId = 0;
    SessionId sessionId = 0;
    Generation generation = 0;
    bool running = false;
    bool paused = false;
    bool fallbackPending = false;
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

void RuntimePlayer::completeSeek(SessionId sessionId, Generation generation)
{
    m_impl->completeSeek(sessionId, generation);
}

void RuntimePlayer::stop()
{
    m_impl->stop();
}

RuntimeDiagnostics RuntimePlayer::diagnostics() const
{
    return m_impl->snapshotDiagnostics();
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
