#pragma once

// 协调 seek 时需要同步触发的播放管线副作用。
// 本协调器只保存非拥有引用，引用到的组件均由 PlaybackPipeline 持有。

#include <QtGlobal>

class AudioRenderer;
class ClockSync;
class FFmpegDecoder;
class VideoRenderer;

class PlaybackSeekCoordinator
{
public:
    PlaybackSeekCoordinator(FFmpegDecoder& decoder,
                            AudioRenderer& audioRenderer,
                            VideoRenderer& videoRenderer,
                            ClockSync& clock);

    void seek(qint64 positionMs, int generation);
    void complete(int generation, int serial);

private:
    FFmpegDecoder& m_decoder;
    AudioRenderer& m_audioRenderer;
    VideoRenderer& m_videoRenderer;
    ClockSync& m_clock;
};
