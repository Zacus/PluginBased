#pragma once

#include "DecodePerformance.h"
#include "FFmpegUtils.h"
#include "HardwareDecoderBackend.h"
#include "media_sdk/Frame.h"
#include "media_sdk/Result.h"

namespace media_sdk {

struct VideoFrameProcessOptions {
    bool preferNativeVideoFrames = true;
};

class VideoFrameProcessor
{
public:
    void reset();

    Result<VideoFrame> process(AVFramePtr frame,
                               VideoFrameProcessOptions options = {},
                               HardwareDecoderBackend* hardwareDecoder = nullptr,
                               DecodePerformanceStats* stats = nullptr);

private:
    AVFramePtr transferHardwareFrameToCpu(AVFramePtr frame,
                                          HardwareDecoderBackend* hardwareDecoder,
                                          DecodePerformanceStats* stats);
    AVFramePtr normalizeVideoFrame(AVFramePtr frame, DecodePerformanceStats* stats);
    Result<VideoFrame> createVideoFrame(AVFramePtr frame, DecodePerformanceStats* stats) const;
    Result<VideoFrame> createNativeVideoFrame(AVFramePtr frame, DecodePerformanceStats* stats) const;
    void copyFrameMetadata(const AVFrame* source, AVFrame* destination) const;

    SwsContextPtr m_videoSwsContext;
};

} // namespace media_sdk
