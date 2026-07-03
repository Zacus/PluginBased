#pragma once

#include "FFmpegUtils.h"
#include "media_sdk/Result.h"

#include <functional>

namespace media_sdk {

class StreamDecoder
{
public:
    enum class FrameHandlerStatus {
        Continue,
        Stop,
        Reject
    };

    using FrameHandler = std::function<FrameHandlerStatus(AVFramePtr frame)>;

    Result<void> sendPacket(AVCodecContext* codecContext,
                            const AVPacket* packet,
                            AVRational timeBase,
                            const FrameHandler& onFrame) const;
    Result<void> flush(AVCodecContext* codecContext,
                       AVRational timeBase,
                       const FrameHandler& onFrame) const;

private:
    Result<void> receiveFrames(AVCodecContext* codecContext,
                               AVRational timeBase,
                               const FrameHandler& onFrame) const;
    void normalizeFramePts(AVFrame* frame, AVRational timeBase) const;
};

} // namespace media_sdk
