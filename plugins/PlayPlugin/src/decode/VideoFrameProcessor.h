#pragma once

// 在解码视频帧进入渲染队列前完成预处理。
// FFmpegDecoder 在解码线程持有该处理器；它只拥有 swscale 缓存状态，不拥有 QObject。

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
