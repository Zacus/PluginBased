#include "PlaybackCompletionTracker.h"

// Implements media completion state transitions for PlayerEngine.
// PlayerEngine remains responsible for public QML state and signal emission.

void PlaybackCompletionTracker::resetForOpen()
{
    m_hasAudio = false;
    m_hasVideo = false;
    m_decoderFinished = false;
    m_audioFinished = false;
    m_videoFinished = false;
    m_mediaFinished = false;
}

void PlaybackCompletionTracker::resetForStop()
{
    m_hasAudio = false;
    m_hasVideo = false;
    m_decoderFinished = false;
    m_audioFinished = false;
    m_videoFinished = false;
    m_mediaFinished = true;
}

void PlaybackCompletionTracker::setStreams(bool hasAudio, bool hasVideo)
{
    m_hasAudio = hasAudio;
    m_hasVideo = hasVideo;
    m_audioFinished = !hasAudio;
    m_videoFinished = !hasVideo;
}

bool PlaybackCompletionTracker::resumeAfterFinishedSeek()
{
    if (!m_mediaFinished)
        return false;

    m_decoderFinished = false;
    m_audioFinished = !m_hasAudio;
    m_videoFinished = !m_hasVideo;
    m_mediaFinished = false;
    return true;
}

void PlaybackCompletionTracker::markDecoderFinished()
{
    m_decoderFinished = true;
    if (!m_hasAudio)
        m_audioFinished = true;
    if (!m_hasVideo)
        m_videoFinished = true;
}

void PlaybackCompletionTracker::markAudioFinished()
{
    m_audioFinished = true;
}

void PlaybackCompletionTracker::markVideoFinished()
{
    m_videoFinished = true;
}

bool PlaybackCompletionTracker::shouldFinish() const
{
    if (!m_decoderFinished)
        return false;
    if (m_hasAudio && !m_audioFinished)
        return false;
    if (m_hasVideo && !m_videoFinished)
        return false;

    return true;
}

bool PlaybackCompletionTracker::finish()
{
    if (m_mediaFinished)
        return false;

    m_mediaFinished = true;
    return true;
}
