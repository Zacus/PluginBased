#include "HardwareDecoderFactory.h"

#if defined(__APPLE__)
#include "VideoToolboxBackend.h"
#endif

namespace media_sdk {

std::unique_ptr<HardwareDecoderBackend> createHardwareDecoderBackend(
    const AVCodec* codec,
    AVCodecID codecId)
{
#if defined(__APPLE__)
    auto backend = std::make_unique<VideoToolboxBackend>();
    if (backend->isAvailableForCodec(codec, codecId))
        return backend;
#else
    (void)codec;
    (void)codecId;
#endif
    return {};
}

} // namespace media_sdk
