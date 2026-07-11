#include "CpuVideoPicturePool.h"

#include <algorithm>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace media_sdk {

struct CpuVideoPicturePool::PoolState : std::enable_shared_from_this<PoolState> {
    explicit PoolState(VideoPicturePoolConfig requestedConfig)
        : config {
            .capacity = std::max<std::size_t>(requestedConfig.capacity, 1),
            .initialRetained = std::min(requestedConfig.initialRetained,
                                        std::max<std::size_t>(requestedConfig.capacity, 1)),
        }
    {
    }

    VideoPictureRef lease(AVFramePtr frame, VideoPictureKey key, std::uint64_t epoch)
    {
        AVFrame* rawFrame = frame.release();
        return VideoPictureRef(rawFrame,
                               [state = shared_from_this(), key, epoch](AVFrame* releasedFrame) {
                                   state->recycle(AVFramePtr(releasedFrame), key, epoch);
                               });
    }

    void recycle(AVFramePtr frame, const VideoPictureKey& key, std::uint64_t epoch)
    {
        if (!frame)
            return;

        frame->pts = AV_NOPTS_VALUE;
        frame->pkt_dts = AV_NOPTS_VALUE;
        frame->duration = 0;
        frame->sample_aspect_ratio = { 0, 1 };
        frame->color_range = AVCOL_RANGE_UNSPECIFIED;
        frame->colorspace = AVCOL_SPC_UNSPECIFIED;
        frame->color_primaries = AVCOL_PRI_UNSPECIFIED;
        frame->color_trc = AVCOL_TRC_UNSPECIFIED;
        frame->chroma_location = AVCHROMA_LOC_UNSPECIFIED;
        frame->crop_top = 0;
        frame->crop_bottom = 0;
        frame->crop_left = 0;
        frame->crop_right = 0;
        frame->flags = 0;
        frame->opaque = nullptr;

        bool retain = false;
        {
            std::lock_guard lock(mutex);
            if (stats.inFlightCount > 0)
                --stats.inFlightCount;
            ++stats.recycleCount;
            retain = !closed && activeKey.has_value() && key == *activeKey && epoch == formatEpoch;
            if (retain)
                freeFrames.push_back(std::move(frame));
            else if (stats.retainedCount > 0)
                --stats.retainedCount;
        }
    }

    mutable std::mutex mutex;
    VideoPicturePoolConfig config;
    VideoPicturePoolStats stats;
    std::vector<AVFramePtr> freeFrames;
    std::optional<VideoPictureKey> activeKey;
    std::size_t reservedAllocations = 0;
    std::uint64_t formatEpoch = 0;
    bool closed = false;
};

namespace {

AVFramePtr allocateFrame(const VideoPictureKey& key)
{
    auto frame = makeFrame();
    if (!frame)
        return {};

    frame->format = key.pixelFormat;
    frame->width = key.width;
    frame->height = key.height;
    const int bufferResult = av_frame_get_buffer(frame.get(), key.alignment);
    if (bufferResult < 0)
        return {};
    if (av_frame_make_writable(frame.get()) < 0)
        return {};
    return frame;
}

} // namespace

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
    AVFramePtr frame;
    std::uint64_t epoch = 0;
    bool reservedAllocation = false;
    {
        std::lock_guard lock(state->mutex);
        if (state->closed)
            return {};

        ++state->stats.acquireCount;
        if (!state->activeKey.has_value())
            state->activeKey = key;
        if (key != *state->activeKey)
            return {};

        epoch = state->formatEpoch;
        const auto reusable = std::find_if(
            state->freeFrames.begin(),
            state->freeFrames.end(),
            [](const AVFramePtr& candidate) {
                return candidate && av_frame_is_writable(candidate.get()) != 0;
            });
        if (reusable != state->freeFrames.end()) {
            frame = std::move(*reusable);
            state->freeFrames.erase(reusable);
            ++state->stats.reuseCount;
        } else if (state->stats.retainedCount + state->reservedAllocations < state->config.capacity) {
            ++state->reservedAllocations;
            reservedAllocation = true;
        } else {
            return {};
        }

        if (frame) {
            ++state->stats.inFlightCount;
            state->stats.highWatermark = std::max(state->stats.highWatermark,
                                                  state->stats.inFlightCount);
        }
    }

    if (frame)
        return state->lease(std::move(frame), key, epoch);

    frame = allocateFrame(key);
    {
        std::lock_guard lock(state->mutex);
        if (reservedAllocation && state->reservedAllocations > 0)
            --state->reservedAllocations;
        if (!frame || state->closed || !state->activeKey.has_value()
            || key != *state->activeKey || epoch != state->formatEpoch) {
            return {};
        }

        ++state->stats.allocationCount;
        ++state->stats.retainedCount;
        ++state->stats.inFlightCount;
        state->stats.highWatermark = std::max(state->stats.highWatermark,
                                              state->stats.inFlightCount);
    }
    return state->lease(std::move(frame), key, epoch);
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
