#include "playback/PlaybackDataBridge.h"

#include "Logger.h"

#include <limits>
#include <utility>

namespace {

bool hasStats(const PlaybackDataBridge::PlaybackDataBridgeStats& stats)
{
    return stats.audioAccepted != 0 ||
           stats.videoAccepted != 0 ||
           stats.audioRejectedStale != 0 ||
           stats.videoRejectedStale != 0 ||
           stats.eofAccepted != 0 ||
           stats.queueAbortFailures != 0;
}

} // namespace

PlaybackDataBridge::PlaybackDataBridge(VideoFrameQueue* videoQueue, AudioFrameQueue* audioQueue)
    : m_videoQueue(videoQueue)
    , m_audioQueue(audioQueue)
{
}

void PlaybackDataBridge::reset(StreamState state)
{
    std::scoped_lock lock(m_mutex);
    m_state = state;
    resetStatsLocked(state);
}

void PlaybackDataBridge::setGeneration(std::uint64_t sessionId, std::uint64_t generation)
{
    std::scoped_lock lock(m_mutex);
    if (m_state.sessionId != sessionId)
        return;
    if (m_state.generation == generation)
        return;
    m_state.generation = generation;
    resetStatsLocked(m_state);
}

void PlaybackDataBridge::cancel()
{
    {
        std::scoped_lock lock(m_mutex);
        m_state = {};
    }
    if (m_videoQueue)
        m_videoQueue->abort();
    if (m_audioQueue)
        m_audioQueue->abort();

    StatsSnapshot snapshot;
    if (takeStatsSnapshot(snapshot))
        logStatsSummary("stop", snapshot);
}

void PlaybackDataBridge::cancelGeneration()
{
    {
        std::scoped_lock lock(m_mutex);
        if (m_state.sessionId != 0)
            m_state.generation = std::numeric_limits<std::uint64_t>::max();
    }
    if (m_videoQueue)
        m_videoQueue->cancelPendingPushes();
    if (m_audioQueue)
        m_audioQueue->cancelPendingPushes();
}

PlaybackDataBridge::PushResult PlaybackDataBridge::pushAudio(
    AVFramePtr frame,
    std::uint64_t sessionId,
    std::uint64_t generation)
{
    if (!frame || !m_audioQueue)
        return { .status = PushStatus::Closed };
    const int queueCancelSerial = m_audioQueue->cancelSerial();
    {
        std::scoped_lock lock(m_mutex);
        if (!acceptsLocked(sessionId, generation))
        {
            incrementCounterLocked(sessionId, generation, Counter::AudioRejectedStale);
            return { .status = PushStatus::StaleGeneration };
        }
    }

    const bool pushed = m_audioQueue->pushIfCancelSerial(std::move(frame),
                                                         queueCancelSerial,
                                                         static_cast<int>(generation));
    incrementCounter(sessionId,
                     generation,
                     pushed ? Counter::AudioAccepted : Counter::QueueAbortFailure);
    return { .status = pushed ? PushStatus::Accepted : PushStatus::Cancelled };
}

PlaybackDataBridge::PushResult PlaybackDataBridge::pushVideo(
    AVFramePtr frame,
    std::uint64_t sessionId,
    std::uint64_t generation)
{
    if (!frame || !m_videoQueue)
        return { .status = PushStatus::Closed };
    const int queueCancelSerial = m_videoQueue->cancelSerial();
    {
        std::scoped_lock lock(m_mutex);
        if (!acceptsLocked(sessionId, generation))
        {
            incrementCounterLocked(sessionId, generation, Counter::VideoRejectedStale);
            return { .status = PushStatus::StaleGeneration };
        }
    }

    const bool pushed = m_videoQueue->pushIfCancelSerial(std::move(frame),
                                                         queueCancelSerial,
                                                         static_cast<int>(generation));
    incrementCounter(sessionId,
                     generation,
                     pushed ? Counter::VideoAccepted : Counter::QueueAbortFailure);
    return { .status = pushed ? PushStatus::Accepted : PushStatus::Cancelled };
}

bool PlaybackDataBridge::finish(std::uint64_t sessionId, std::uint64_t generation)
{
    const int videoCancelSerial = m_videoQueue ? m_videoQueue->cancelSerial() : 0;
    const int audioCancelSerial = m_audioQueue ? m_audioQueue->cancelSerial() : 0;
    const StreamState state = snapshot();
    if (!accepts(state, sessionId, generation))
        return false;

    bool accepted = true;
    if (state.hasVideo && m_videoQueue)
    {
        const bool videoFinished = m_videoQueue->finishIfCancelSerial(videoCancelSerial,
                                                                       static_cast<int>(generation));
        if (!videoFinished)
            incrementCounter(sessionId, generation, Counter::QueueAbortFailure);
        accepted = videoFinished && accepted;
    }
    if (state.hasAudio && m_audioQueue)
    {
        const bool audioFinished = m_audioQueue->finishIfCancelSerial(audioCancelSerial,
                                                                       static_cast<int>(generation));
        if (!audioFinished)
            incrementCounter(sessionId, generation, Counter::QueueAbortFailure);
        accepted = audioFinished && accepted;
    }
    if (accepted)
    {
        incrementCounter(sessionId, generation, Counter::EofAccepted);
        StatsSnapshot stats;
        if (takeStatsSnapshot(stats))
            logStatsSummary("eof", stats);
    }
    return accepted;
}

PlaybackDataBridge::StreamState PlaybackDataBridge::snapshot() const
{
    std::scoped_lock lock(m_mutex);
    return m_state;
}

bool PlaybackDataBridge::accepts(const StreamState& state,
                                 std::uint64_t sessionId,
                                 std::uint64_t generation) const
{
    return state.sessionId == sessionId && state.generation == generation;
}

bool PlaybackDataBridge::acceptsLocked(std::uint64_t sessionId, std::uint64_t generation) const
{
    return accepts(m_state, sessionId, generation);
}

void PlaybackDataBridge::incrementCounter(std::uint64_t sessionId,
                                          std::uint64_t generation,
                                          Counter counter)
{
    std::scoped_lock lock(m_mutex);
    incrementCounterLocked(sessionId, generation, counter);
}

void PlaybackDataBridge::incrementCounterLocked(std::uint64_t sessionId,
                                                std::uint64_t generation,
                                                Counter counter)
{
    if (!accepts(m_statsState, sessionId, generation))
        return;

    switch (counter)
    {
    case Counter::AudioAccepted:
        ++m_stats.audioAccepted;
        break;
    case Counter::VideoAccepted:
        ++m_stats.videoAccepted;
        break;
    case Counter::AudioRejectedStale:
        ++m_stats.audioRejectedStale;
        break;
    case Counter::VideoRejectedStale:
        ++m_stats.videoRejectedStale;
        break;
    case Counter::EofAccepted:
        ++m_stats.eofAccepted;
        break;
    case Counter::QueueAbortFailure:
        ++m_stats.queueAbortFailures;
        break;
    }
}

bool PlaybackDataBridge::takeStatsSnapshotLocked(StatsSnapshot& snapshot)
{
    if (m_statsLogged || m_statsState.sessionId == 0 || !hasStats(m_stats))
        return false;

    snapshot = {
        .state = m_statsState,
        .stats = m_stats,
    };
    m_statsLogged = true;
    return true;
}

bool PlaybackDataBridge::takeStatsSnapshot(StatsSnapshot& snapshot)
{
    std::scoped_lock lock(m_mutex);
    return takeStatsSnapshotLocked(snapshot);
}

void PlaybackDataBridge::resetStatsLocked(StreamState state)
{
    m_statsState = state;
    m_stats = {};
    m_statsLogged = state.sessionId == 0;
}

void PlaybackDataBridge::logStatsSummary(const char* reason, const StatsSnapshot& snapshot) const
{
    LOG_INFO("PlayDataBridge: session={} generation={} audioAccepted={} videoAccepted={} "
             "staleAudio={} staleVideo={} eofAccepted={} queueAbortFailures={} reason={}",
             snapshot.state.sessionId,
             snapshot.state.generation,
             snapshot.stats.audioAccepted,
             snapshot.stats.videoAccepted,
             snapshot.stats.audioRejectedStale,
             snapshot.stats.videoRejectedStale,
             snapshot.stats.eofAccepted,
             snapshot.stats.queueAbortFailures,
             reason);
}
