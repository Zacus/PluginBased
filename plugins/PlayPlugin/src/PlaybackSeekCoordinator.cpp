#include "PlaybackSeekCoordinator.h"

// Implements ordered seek coordination across decoder, renderers, queues, and clock.
// The call order mirrors the previous PlaybackPipeline logic to preserve playback behavior.

#include "AudioRenderer.h"
#include "ClockSync.h"
#include "FFmpegDecoder.h"
#include "VideoRenderer.h"

PlaybackSeekCoordinator::PlaybackSeekCoordinator(FFmpegDecoder& decoder,
                                                 AudioRenderer& audioRenderer,
                                                 VideoRenderer& videoRenderer,
                                                 ClockSync& clock)
    : m_decoder(decoder)
    , m_audioRenderer(audioRenderer)
    , m_videoRenderer(videoRenderer)
    , m_clock(clock)
{
}

void PlaybackSeekCoordinator::seek(qint64 positionMs, int generation)
{
    m_videoRenderer.beginSeek(generation);
    m_audioRenderer.setAcceptedSerial(generation);
    m_decoder.seekTo(positionMs, generation);
    m_audioRenderer.flush();
    m_videoRenderer.flush();
    m_clock.invalidate();
}

void PlaybackSeekCoordinator::complete(int generation, int serial)
{
    m_audioRenderer.setAcceptedSerial(serial);
    m_videoRenderer.completeSeek(generation, serial);
}
