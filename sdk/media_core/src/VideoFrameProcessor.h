#pragma once

#include "CpuVideoPicturePool.h"
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
    VideoFrameProcessor();
    ~VideoFrameProcessor();

    void reset();

    Result<VideoFrame> process(AVFramePtr frame,
                               VideoFrameProcessOptions options = {},
                               HardwareDecoderBackend* hardwareDecoder = nullptr,
                               DecodePerformanceStats* stats = nullptr);
    [[nodiscard]] VideoPicturePoolStats picturePoolStats() const;

private:
    AVFramePtr transferHardwareFrameToCpu(AVFramePtr frame,
                                          HardwareDecoderBackend* hardwareDecoder,
                                          DecodePerformanceStats* stats);
    VideoPictureRef normalizeVideoFrame(AVFramePtr frame, DecodePerformanceStats* stats);
    Result<VideoFrame> createVideoFrame(VideoPictureRef frame) const;
    Result<VideoFrame> createNativeVideoFrame(AVFramePtr frame, DecodePerformanceStats* stats) const;
    void copyFrameMetadata(const AVFrame* source, AVFrame* destination) const;

    SwsContextPtr m_videoSwsContext;
    std::unique_ptr<CpuVideoPicturePool> m_cpuPicturePool;
};

} // namespace media_sdk
