#pragma once

// 向 FFmpeg 编解码器发送包，并回调输出已归一化时间戳的解码帧。
// FFmpegDecoder 持有一个该值类型辅助对象，并只在解码线程调用它。

#include "common/FFmpegUtils.h"

#include <functional>

class StreamFrameDecoder
{
public:
    using FrameHandler = std::function<bool(AVFramePtr frame)>;

    bool sendPacket(AVCodecContext* codecContext,
                    AVPacket* packet,
                    AVRational timeBase,
                    const FrameHandler& onFrame) const;

private:
    void normalizeFramePts(AVFrame* frame, AVRational timeBase) const;
};
