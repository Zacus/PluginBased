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

struct PlayerDiagnostics {
    VideoPicturePoolSnapshot videoPicturePool;
};

} // namespace media_sdk
