#include "media_sdk/runtime/RuntimePlayer.h"

#include "AvSyncScheduler.h"
#include "NativeFallbackController.h"
#include "PresentTracker.h"
#include "RuntimeFrameQueue.h"
#include "media_sdk/Error.h"

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

    Result<void> open()
    {
        if (!dependencies.audioOutput || !dependencies.videoPresenter) {
            return Result<void>::failure({
                .code = MediaErrorCode::InternalStateError,
                .message = "RuntimePlayer requires audio output and video presenter",
                .detail = {},
            });
        }

        stop();

        diagnostics = {};
        sessionId = ++lastSessionId;
        generation = 1;
        running = true;
        paused = false;

        audioQueue.reset(sessionId, generation);
        videoQueue.reset(sessionId, generation);
        scheduler.reset(generation);
        presentTracker.reset(sessionId, generation);
        presentTracker.setMaxPending(dependencies.videoPresenter->capabilities().maxPendingFrames);
        fallbackController.reset(sessionId, generation, config.outputPolicy);
        dependencies.videoPresenter->setEvents(&owner);
        return Result<void>::success();
    }

    void enqueueAudio(RuntimeAudioFrame frame)
    {
        if (!running)
            return;

        const auto pushResult = audioQueue.push(std::move(frame));
        if (pushResult != RuntimeFrameQueue<RuntimeAudioFrame>::PushResult::Accepted)
            return;

        ++diagnostics.audioQueued;

        RuntimeAudioFrame queuedFrame;
        if (audioQueue.waitPop(queuedFrame) != RuntimeFrameQueue<RuntimeAudioFrame>::PopResult::Frame)
            return;

        if (!isCurrent(queuedFrame.sessionId, queuedFrame.generation))
            return;

        const auto samples = queuedFrame.frame.samples();
        if (dependencies.audioOutput->write({
                .bytes = samples,
                .pts = queuedFrame.frame.pts(),
                .generation = queuedFrame.generation,
            }).ok()) {
            ++diagnostics.audioWritten;
        }
    }

    void enqueueVideo(RuntimeVideoFrame frame)
    {
        if (!running || paused)
            return;

        const bool nativeFrame = frame.frame.pixelFormat() == PixelFormat::Native;
        const auto pushResult = videoQueue.push(std::move(frame));
        if (pushResult != RuntimeFrameQueue<RuntimeVideoFrame>::PushResult::Accepted)
            return;

        ++diagnostics.videoQueued;
        if (nativeFrame)
            ++diagnostics.nativeAccepted;

        RuntimeVideoFrame queuedFrame;
        if (videoQueue.waitPop(queuedFrame) != RuntimeFrameQueue<RuntimeVideoFrame>::PopResult::Frame)
            return;

        if (!isCurrent(queuedFrame.sessionId, queuedFrame.generation))
            return;

        const auto clock = dependencies.audioOutput->clock();
        const auto decision = scheduler.decide(queuedFrame.frame.pts(), clock, generation);
        if (decision.action == VideoScheduleAction::Drop) {
            ++diagnostics.videoDroppedLate;
            return;
        }

        if (decision.action == VideoScheduleAction::Wait) {
            ++diagnostics.videoWaited;
            return;
        }

        const auto clockPosition = clock.valid && clock.generation == generation
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
    }

    void enqueueEndOfStream(SessionId eofSessionId, Generation eofGeneration)
    {
        if (!running || !isCurrent(eofSessionId, eofGeneration))
            return;

        const auto audioAccepted = audioQueue.pushEndOfStream(eofSessionId, eofGeneration)
            == RuntimeFrameQueue<RuntimeAudioFrame>::PushResult::Accepted;
        const auto videoAccepted = videoQueue.pushEndOfStream(eofSessionId, eofGeneration)
            == RuntimeFrameQueue<RuntimeVideoFrame>::PushResult::Accepted;

        if (!audioAccepted || !videoAccepted)
            return;

        ++diagnostics.eofAccepted;
        RuntimeAudioFrame audioEof;
        RuntimeVideoFrame videoEof;
        const auto audioPop = audioQueue.waitPop(audioEof);
        const auto videoPop = videoQueue.waitPop(videoEof);
        if (audioPop == RuntimeFrameQueue<RuntimeAudioFrame>::PopResult::EndOfStream
            && videoPop == RuntimeFrameQueue<RuntimeVideoFrame>::PopResult::EndOfStream) {
            ++diagnostics.eofPresented;
        }
    }

    void pause()
    {
        if (!running || paused)
            return;

        paused = true;
        dependencies.audioOutput->pause();
    }

    void resume()
    {
        if (!running || !paused)
            return;

        paused = false;
        dependencies.audioOutput->resume();
    }

    void seek(std::chrono::microseconds)
    {
        if (!running)
            return;

        ++generation;
        audioQueue.abort();
        videoQueue.abort();
        diagnostics.queueAbortCount += 2;
        audioQueue.reset(sessionId, generation);
        videoQueue.reset(sessionId, generation);
        presentTracker.clear();
        presentTracker.reset(sessionId, generation);
        presentTracker.setMaxPending(dependencies.videoPresenter->capabilities().maxPendingFrames);
        dependencies.videoPresenter->clear();
        dependencies.audioOutput->flush();
        scheduler.reset(generation);
        fallbackController.reset(sessionId, generation, config.outputPolicy);
    }

    void stop()
    {
        if (!running)
            return;

        audioQueue.abort();
        videoQueue.abort();
        diagnostics.queueAbortCount += 2;
        dependencies.audioOutput->pause();
        dependencies.audioOutput->flush();
        dependencies.audioOutput->close();
        dependencies.videoPresenter->clear();
        presentTracker.clear();
        running = false;
        paused = false;
        generation = 0;
    }

    void onPresentComplete(PresentCompletion completion)
    {
        if (!running)
            return;

        const auto action = presentTracker.complete(sessionId, generation, std::move(completion));
        if (action == PresentCompletionAction::AcceptedFailure)
            handlePresentFailure(PresentStatus::Failed);
    }

    void handlePresentFailure(PresentStatus reason)
    {
        const auto clock = dependencies.audioOutput->clock();
        const auto transition = fallbackController.beginFallback({
            .sessionId = sessionId,
            .generation = generation,
            .reason = reason,
            .resumePosition = clock.position,
        });
        if (!transition.accepted)
            return;

        ++diagnostics.nativeFallbacks;
        config.outputPolicy = transition.newPolicy;

        if (transition.pauseAudio)
            dependencies.audioOutput->pause();
        if (transition.flushAudio)
            dependencies.audioOutput->flush();
        if (transition.abortQueues) {
            audioQueue.abort();
            videoQueue.abort();
            diagnostics.queueAbortCount += 2;
        }
        if (transition.clearPresenter)
            dependencies.videoPresenter->clear();

        presentTracker.clear();
        generation = transition.newGeneration;
        audioQueue.reset(sessionId, generation);
        videoQueue.reset(sessionId, generation);
        presentTracker.reset(sessionId, generation);
        presentTracker.setMaxPending(dependencies.videoPresenter->capabilities().maxPendingFrames);
        scheduler.reset(generation);
    }

    bool isCurrent(SessionId checkedSessionId, Generation checkedGeneration) const
    {
        return checkedSessionId == sessionId && checkedGeneration == generation;
    }

    RuntimePlayer& owner;
    RuntimePlayerConfig config;
    RuntimePlayerDependencies dependencies;
    RuntimeFrameQueue<RuntimeAudioFrame> audioQueue;
    RuntimeFrameQueue<RuntimeVideoFrame> videoQueue;
    AvSyncScheduler scheduler;
    PresentTracker presentTracker;
    NativeFallbackController fallbackController;
    RuntimeDiagnostics diagnostics {};
    SessionId lastSessionId = 0;
    SessionId sessionId = 0;
    Generation generation = 0;
    bool running = false;
    bool paused = false;
};

RuntimePlayer::RuntimePlayer(RuntimePlayerConfig config, RuntimePlayerDependencies dependencies)
    : m_impl(std::make_unique<Impl>(*this, config, dependencies))
{
}

RuntimePlayer::~RuntimePlayer()
{
    if (m_impl) {
        m_impl->stop();
        if (m_impl->dependencies.videoPresenter)
            m_impl->dependencies.videoPresenter->setEvents(nullptr);
    }
}

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

void RuntimePlayer::stop()
{
    m_impl->stop();
}

RuntimeDiagnostics RuntimePlayer::diagnostics() const
{
    return m_impl->diagnostics;
}

void RuntimePlayer::onPresentComplete(PresentCompletion completion)
{
    m_impl->onPresentComplete(std::move(completion));
}

} // namespace media_sdk::runtime
