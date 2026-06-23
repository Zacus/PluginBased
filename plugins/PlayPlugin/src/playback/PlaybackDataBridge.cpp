#include "playback/PlaybackDataBridge.h"

#include <utility>

PlaybackDataBridge::PlaybackDataBridge(VideoFrameQueue* videoQueue, AudioFrameQueue* audioQueue)
    : m_videoQueue(videoQueue)
    , m_audioQueue(audioQueue)
{
}

void PlaybackDataBridge::reset(StreamState state)
{
    std::scoped_lock lock(m_mutex);
    m_state = state;
}

void PlaybackDataBridge::setGeneration(std::uint64_t sessionId, std::uint64_t generation)
{
    std::scoped_lock lock(m_mutex);
    if (m_state.sessionId != sessionId)
        return;
    m_state.generation = generation;
}

void PlaybackDataBridge::cancel()
{
    std::scoped_lock lock(m_mutex);
    m_state = {};
}

bool PlaybackDataBridge::pushAudio(AVFramePtr frame,
                                   std::uint64_t sessionId,
                                   std::uint64_t generation)
{
    const StreamState state = snapshot();
    if (!frame || !m_audioQueue || !accepts(state, sessionId, generation))
        return false;
    return m_audioQueue->push(std::move(frame), static_cast<int>(generation));
}

bool PlaybackDataBridge::pushVideo(AVFramePtr frame,
                                   std::uint64_t sessionId,
                                   std::uint64_t generation)
{
    const StreamState state = snapshot();
    if (!frame || !m_videoQueue || !accepts(state, sessionId, generation))
        return false;
    return m_videoQueue->push(std::move(frame), static_cast<int>(generation));
}

bool PlaybackDataBridge::finish(std::uint64_t sessionId, std::uint64_t generation)
{
    const StreamState state = snapshot();
    if (!accepts(state, sessionId, generation))
        return false;

    bool accepted = true;
    if (state.hasVideo && m_videoQueue)
        accepted = m_videoQueue->finish(static_cast<int>(generation)) && accepted;
    if (state.hasAudio && m_audioQueue)
        accepted = m_audioQueue->finish(static_cast<int>(generation)) && accepted;
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
