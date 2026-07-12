#pragma once

#include "FFmpegUtils.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace media_sdk {

struct VideoPictureKey {
    int width = 0;
    int height = 0;
    AVPixelFormat pixelFormat = AV_PIX_FMT_NONE;
    int alignment = 32;

    [[nodiscard]] bool isValid() const;

    friend bool operator==(const VideoPictureKey&, const VideoPictureKey&) = default;
};

struct VideoPicturePoolConfig {
    std::size_t capacity = 12;
    std::size_t initialRetained = 3;
};

struct VideoPicturePoolStats {
    std::uint64_t acquireCount = 0;
    std::uint64_t reuseCount = 0;
    std::uint64_t allocationCount = 0;
    std::uint64_t recycleCount = 0;
    std::uint64_t transientAllocationCount = 0;
    std::uint64_t incompatibleReturnCount = 0;
    std::uint64_t highWatermark = 0;
    std::uint64_t retainedCount = 0;
    std::uint64_t inFlightCount = 0;
};

using VideoPictureRef = std::shared_ptr<AVFrame>;

class CpuVideoPicturePool final
{
public:
    explicit CpuVideoPicturePool(VideoPicturePoolConfig config = {});
    ~CpuVideoPicturePool();

    CpuVideoPicturePool(const CpuVideoPicturePool&) = delete;
    CpuVideoPicturePool& operator=(const CpuVideoPicturePool&) = delete;
    CpuVideoPicturePool(CpuVideoPicturePool&&) = delete;
    CpuVideoPicturePool& operator=(CpuVideoPicturePool&&) = delete;

    [[nodiscard("an empty reference means the key is invalid, allocation failed, or the pool is closed")]]
    VideoPictureRef acquire(const VideoPictureKey& key);
    void reset();
    void close();
    [[nodiscard]] VideoPicturePoolStats stats() const;

private:
    struct PoolState;
    std::shared_ptr<PoolState> m_state;
};

} // namespace media_sdk
