#pragma once

#include "FFmpegUtils.h"

#include <string_view>

namespace media_sdk {

class HardwareDecoderBackend
{
public:
    virtual ~HardwareDecoderBackend() = default;

    virtual std::string_view name() const = 0;
    virtual bool isAvailableForCodec(const AVCodec* codec, AVCodecID codecId) const = 0;
    virtual bool configureContext(AVCodecContext* codecContext) = 0;
    virtual bool isHardwareFrame(const AVFrame* frame) const = 0;
    virtual AVFramePtr transferToCpuFrame(const AVFrame* frame) = 0;
    virtual void reset() = 0;
};

} // namespace media_sdk
