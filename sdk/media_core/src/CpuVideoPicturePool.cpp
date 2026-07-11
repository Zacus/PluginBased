#include "CpuVideoPicturePool.h"

#include <algorithm>
#include <mutex>
#include <utility>

namespace media_sdk {

struct CpuVideoPicturePool::PoolState {
    explicit PoolState(VideoPicturePoolConfig requestedConfig)
        : config {
            .capacity = std::max<std::size_t>(requestedConfig.capacity, 1),
            .initialRetained = std::min(requestedConfig.initialRetained,
                                        std::max<std::size_t>(requestedConfig.capacity, 1)),
        }
    {
    }

    mutable std::mutex mutex;
    VideoPicturePoolConfig config;
    VideoPicturePoolStats stats;
    bool closed = false;
};

bool VideoPictureKey::isValid() const
{
    return width > 0 && height > 0 && pixelFormat != AV_PIX_FMT_NONE && alignment > 0;
}

CpuVideoPicturePool::CpuVideoPicturePool(VideoPicturePoolConfig config)
    : m_state(std::make_shared<PoolState>(config))
{
}

CpuVideoPicturePool::~CpuVideoPicturePool()
{
    close();
}

VideoPictureRef CpuVideoPicturePool::acquire(const VideoPictureKey& key)
{
    if (!key.isValid())
        return {};

    const auto state = m_state;
    std::lock_guard lock(state->mutex);
    if (state->closed)
        return {};

    ++state->stats.acquireCount;
    return {};
}

void CpuVideoPicturePool::close()
{
    const auto state = m_state;
    std::lock_guard lock(state->mutex);
    state->closed = true;
}

VideoPicturePoolStats CpuVideoPicturePool::stats() const
{
    const auto state = m_state;
    std::lock_guard lock(state->mutex);
    return state->stats;
}

} // namespace media_sdk
