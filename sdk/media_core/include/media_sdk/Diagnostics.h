#pragma once

#include <cstdint>

namespace media_sdk {

struct VideoPicturePoolSnapshot {
    std::uint64_t acquireCount = 0;
    std::uint64_t reuseCount = 0;
    std::uint64_t allocationCount = 0;
    std::uint64_t transientAllocationCount = 0;
    std::uint64_t highWatermark = 0;
    std::uint64_t retainedCount = 0;
    std::uint64_t inFlightCount = 0;
};

struct DecoderBufferPoolSnapshot {
    std::uint64_t callbackCount = 0;
    std::uint64_t pooledFrameCount = 0;
    std::uint64_t fallbackCount = 0;
    std::uint64_t poolRebuildCount = 0;
    std::uint64_t planeAcquireCount = 0;
    std::uint64_t planeAllocationCount = 0;
};

struct PlayerDiagnostics {
    VideoPicturePoolSnapshot videoPicturePool;
    DecoderBufferPoolSnapshot decoderBufferPool;
};

} // namespace media_sdk
