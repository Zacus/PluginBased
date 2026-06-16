#pragma once

// Sends packets to an FFmpeg codec and emits normalized decoded frames.
// FFmpegDecoder owns one value helper and invokes it on the decode thread.

#include "FFmpegUtils.h"

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
