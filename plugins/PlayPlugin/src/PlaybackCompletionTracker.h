#pragma once

// Tracks decoder/audio/video completion state for PlayerEngine.
// This value helper keeps playback-finished rules out of the QML-facing facade.

class PlaybackCompletionTracker
{
public:
    void resetForOpen();
    void resetForStop();
    void setStreams(bool hasAudio, bool hasVideo);

    bool hasAudio() const { return m_hasAudio; }
    bool hasVideo() const { return m_hasVideo; }
    bool isMediaFinished() const { return m_mediaFinished; }

    bool resumeAfterFinishedSeek();
    void markDecoderFinished();
    void markAudioFinished();
    void markVideoFinished();
    bool shouldFinish() const;
    bool finish();

private:
    bool m_hasAudio = false;
    bool m_hasVideo = false;
    bool m_decoderFinished = false;
    bool m_audioFinished = false;
    bool m_videoFinished = false;
    bool m_mediaFinished = false;
};
