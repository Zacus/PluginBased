#pragma once

#include "HardwareDecoderBackend.h"

namespace media_sdk {

class VideoToolboxBackend final : public HardwareDecoderBackend
{
public:
    std::string_view name() const override;
    bool isAvailableForCodec(const AVCodec* codec, AVCodecID codecId) const override;
    bool configureContext(AVCodecContext* codecContext) override;
    bool isHardwareFrame(const AVFrame* frame) const override;
    AVFramePtr transferToCpuFrame(const AVFrame* frame) override;
    void reset() override;

private:
    AVBufferRefPtr m_deviceContext;
};

} // namespace media_sdk
