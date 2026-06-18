#pragma once

#include "common/FFmpegUtils.h"

#include <QString>

class HardwareDecoderBackend
{
public:
    virtual ~HardwareDecoderBackend() = default;

    virtual QString name() const = 0;
    virtual bool isAvailableForCodec(const AVCodec* codec, AVCodecID codecId) const = 0;
    virtual bool configureContext(AVCodecContext* codecContext) = 0;
    virtual bool isHardwareFrame(const AVFrame* frame) const = 0;
    virtual AVFramePtr transferToCpuFrame(const AVFrame* frame) = 0;
    virtual void reset() = 0;
};
