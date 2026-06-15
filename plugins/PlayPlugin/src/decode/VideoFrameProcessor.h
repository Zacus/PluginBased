#pragma once

// Prepares decoded video frames before they enter the renderer queue.
// FFmpegDecoder owns this processor on the decode thread; it owns swscale cache state and no QObjects.

#include "FFmpegUtils.h"
#include "decode/DecodePerformance.h"

class HardwareDecoderBackend;

class VideoFrameProcessor
{
public:
    void reset();

    AVFramePtr prepareForQueue(AVFramePtr frame,
                               HardwareDecoderBackend* hardwareDecoder,
                               bool directRenderingEnabled,
                               DecodePerformanceStats& stats);

private:
    bool shouldPreserveHardwareFrameForDirectRender(const AVFrame* frame,
                                                    HardwareDecoderBackend* hardwareDecoder,
                                                    bool directRenderingEnabled) const;
    AVFramePtr transferHardwareFrameToCpu(AVFramePtr frame,
                                          HardwareDecoderBackend* hardwareDecoder,
                                          DecodePerformanceStats& stats);
    AVFramePtr normalizeVideoFrame(AVFramePtr frame, DecodePerformanceStats& stats);
    void copyFrameMetadata(const AVFrame* source, AVFrame* destination) const;

    SwsContextPtr m_videoSwsCtx;
    int m_hardwareTransferFailureCount = 0;
};
