#pragma once

// Coordinates playback seek side effects owned by PlaybackPipeline.
// The coordinator stores non-owning references; PlaybackPipeline owns every referenced component.

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
