#pragma once

// 跟踪解码器、音频和视频三路完成状态，供 PlayerEngine 判断播放是否结束。
// 该值类型把“播放完成”规则从 QML 门面类中拆出，便于单独维护。

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
