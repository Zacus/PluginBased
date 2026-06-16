#include "PlaybackSeekCoordinator.h"

// 按固定顺序协调解码器、渲染器、队列和时钟上的 seek 操作。
// 调用顺序保持与拆分前 PlaybackPipeline 逻辑一致，确保播放行为不变。

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
