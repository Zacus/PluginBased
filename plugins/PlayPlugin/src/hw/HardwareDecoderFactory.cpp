#include "HardwareDecoderFactory.h"

#include <QtGlobal>

#if defined(Q_OS_APPLE)
#include "VideoToolboxBackend.h"
#endif
#if defined(Q_OS_WIN)
#include "D3D11VABackend.h"
#endif
#if defined(Q_OS_LINUX)
#include "VaapiBackend.h"
#endif

std::unique_ptr<HardwareDecoderBackend> createHardwareDecoderBackend(const AVCodec* codec,
                                                                     AVCodecID codecId)
{
#if defined(Q_OS_APPLE)
    auto backend = std::make_unique<VideoToolboxBackend>();
    if (backend->isAvailableForCodec(codec, codecId))
        return backend;
#endif

#if defined(Q_OS_WIN)
    auto backend = std::make_unique<D3D11VABackend>();
    if (backend->isAvailableForCodec(codec, codecId))
        return backend;
#endif

#if defined(Q_OS_LINUX)
    auto backend = std::make_unique<VaapiBackend>();
    if (backend->isAvailableForCodec(codec, codecId))
        return backend;
#endif

    return {};
}
