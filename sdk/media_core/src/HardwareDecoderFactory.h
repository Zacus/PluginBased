#pragma once

#include "HardwareDecoderBackend.h"

#include <memory>

namespace media_sdk {

std::unique_ptr<HardwareDecoderBackend> createHardwareDecoderBackend(
    const AVCodec* codec,
    AVCodecID codecId);

} // namespace media_sdk
