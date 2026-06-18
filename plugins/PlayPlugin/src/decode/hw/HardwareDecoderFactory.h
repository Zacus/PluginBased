#pragma once

#include "HardwareDecoderBackend.h"

#include <memory>

std::unique_ptr<HardwareDecoderBackend> createHardwareDecoderBackend(const AVCodec* codec,
                                                                     AVCodecID codecId);
