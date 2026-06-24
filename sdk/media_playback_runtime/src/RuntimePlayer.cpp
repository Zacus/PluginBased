#include "media_sdk/runtime/RuntimePlayer.h"

#include "AvSyncScheduler.h"
#include "NativeFallbackController.h"
#include "PresentTracker.h"
#include "RuntimeFrameQueue.h"
#include "media_sdk/Error.h"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

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

    void enqueueAudio(RuntimeAudioFrame frame)
    {
        if (!isRunning())
            return;

        const auto pushResult = audioQueue.push(std::move(frame));
        if (pushResult != RuntimeFrameQueue<RuntimeAudioFrame>::PushResult::Accepted)
            return;

        std::lock_guard lock(m_mutex);
        ++diagnostics.audioQueued;
    }

    void enqueueVideo(RuntimeVideoFrame frame)
    {
        if (!isAcceptingVideo())
            return;

        const bool nativeFrame = frame.frame.pixelFormat() == PixelFormat::Native;
        const auto pushResult = videoQueue.push(std::move(frame));
        if (pushResult != RuntimeFrameQueue<RuntimeVideoFrame>::PushResult::Accepted)
            return;

        std::lock_guard lock(m_mutex);
        ++diagnostics.videoQueued;
        if (nativeFrame)
            ++diagnostics.nativeAccepted;
    }

    void enqueueEndOfStream(SessionId eofSessionId, Generation eofGeneration)
    {
        if (!isCurrent(eofSessionId, eofGeneration))
            return;

        const auto audioAccepted = audioQueue.pushEndOfStream(eofSessionId, eofGeneration)
            == RuntimeFrameQueue<RuntimeAudioFrame>::PushResult::Accepted;
        const auto videoAccepted = videoQueue.pushEndOfStream(eofSessionId, eofGeneration)
            == RuntimeFrameQueue<RuntimeVideoFrame>::PushResult::Accepted;

        if (!audioAccepted || !videoAccepted)
            return;

        std::lock_guard lock(m_mutex);
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

        if (shouldResume)
            dependencies.audioOutput->resume();
        m_controlChanged.notify_all();
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

        if (shouldResume)
            dependencies.audioOutput->resume();
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
        joinWorkers();

        dependencies.audioOutput->pause();
        dependencies.audioOutput->flush();
        dependencies.audioOutput->close();
        dependencies.videoPresenter->clear();

        std::lock_guard lock(m_mutex);
        presentTracker.clear();
        audioEofSeen = false;
        videoEofSeen = false;
    }

    RuntimeDiagnostics snapshotDiagnostics() const
    {
        std::lock_guard lock(m_mutex);
        return diagnostics;
    }

    void onPresentComplete(PresentCompletion completion)
    {
        bool failed = false;
        PresentStatus failureStatus = completion.status;
        {
            std::lock_guard lock(m_mutex);
            if (!running)
                return;

            const auto action = presentTracker.complete(sessionId, generation, std::move(completion));
            failed = action == PresentCompletionAction::AcceptedFailure;
        }

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
            const auto writeResult = dependencies.audioOutput->write({
                .bytes = samples,
                .pts = queuedFrame.frame.pts(),
                .generation = queuedFrame.generation,
            });
            if (writeResult.ok()) {
                std::lock_guard lock(m_mutex);
                ++diagnostics.audioWritten;
            }
        }
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
            const auto clock = dependencies.audioOutput->clock();
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
            const bool presentedNativeFrame = queuedFrame.frame.pixelFormat() == PixelFormat::Native;
            const auto result = dependencies.videoPresenter->present(std::move(queuedFrame.frame), timing);
            if (isFailureStatus(result.status)) {
                handlePresentFailure(result.status);
                return;
            }

            std::lock_guard lock(m_mutex);
            ++diagnostics.videoPresented;
            if (presentedNativeFrame)
                ++diagnostics.nativePresented;
            else
                ++diagnostics.cpuPresented;

            if (result.status == PresentStatus::Queued && result.id != 0) {
                presentTracker.track({
                    .id = result.id,
                    .sessionId = sessionId,
                    .generation = generation,
                    .nativeFrame = presentedNativeFrame,
                });
            }
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

    void markEof(bool audio, SessionId checkedSessionId, Generation checkedGeneration)
    {
        std::lock_guard lock(m_mutex);
        if (!running || !isCurrentLocked(checkedSessionId, checkedGeneration))
            return;

        if (audio)
            audioEofSeen = true;
        else
            videoEofSeen = true;

        if (audioEofSeen && videoEofSeen)
            ++diagnostics.eofPresented;
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
    mutable std::mutex m_mutex;
    std::condition_variable m_controlChanged;
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

void RuntimePlayer::enqueueAudio(RuntimeAudioFrame frame)
{
    m_impl->enqueueAudio(std::move(frame));
}

void RuntimePlayer::enqueueVideo(RuntimeVideoFrame frame)
{
    m_impl->enqueueVideo(std::move(frame));
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

void RuntimePlayer::onPresentComplete(PresentCompletion completion)
{
    m_impl->onPresentComplete(std::move(completion));
}

} // namespace media_sdk::runtime
