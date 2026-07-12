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

    VideoPictureRef lease(AVFramePtr frame,
                          VideoPictureKey key,
                          std::uint64_t epoch,
                          bool retained)
    {
        AVFrame* rawFrame = frame.release();
        return VideoPictureRef(rawFrame,
                               [state = shared_from_this(), key, epoch, retained](
                                   AVFrame* releasedFrame) {
                                   state->recycle(AVFramePtr(releasedFrame), key, epoch, retained);
                               });
    }

    void recycle(AVFramePtr frame,
                 const VideoPictureKey& key,
                 std::uint64_t epoch,
                 bool retained)
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
            if (!retained)
                return;

            ++stats.recycleCount;
            retain = !closed && activeKey.has_value() && key == *activeKey && epoch == formatEpoch;
            if (retain)
                freeFrames.push_back(std::move(frame));
            else {
                if (!closed)
                    ++stats.incompatibleReturnCount;
                if (stats.retainedCount > 0)
                    --stats.retainedCount;
            }
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

std::vector<AVFramePtr> allocateFrames(const VideoPictureKey& key, std::size_t count)
{
    std::vector<AVFramePtr> frames;
    frames.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto frame = allocateFrame(key);
        if (!frame)
            break;
        frames.push_back(std::move(frame));
    }
    return frames;
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
    std::vector<AVFramePtr> discardedFrames;
    std::uint64_t epoch = 0;
    std::size_t allocationReservation = 0;
    bool transientAllocation = false;
    {
        std::lock_guard lock(state->mutex);
        if (state->closed)
            return {};

        ++state->stats.acquireCount;
        const bool keyChanged = !state->activeKey.has_value() || key != *state->activeKey;
        if (keyChanged) {
            const std::size_t discardedCount = state->freeFrames.size();
            discardedFrames = std::move(state->freeFrames);
            if (state->stats.retainedCount >= discardedCount)
                state->stats.retainedCount -= discardedCount;
            else
                state->stats.retainedCount = 0;
            state->activeKey = key;
            ++state->formatEpoch;
        }

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
            const std::size_t available = state->config.capacity
                - state->stats.retainedCount
                - state->reservedAllocations;
            const std::size_t requested = keyChanged
                ? std::max<std::size_t>(state->config.initialRetained, 1)
                : 1;
            allocationReservation = std::min(available, requested);
            state->reservedAllocations += allocationReservation;
        } else {
            transientAllocation = true;
        }

        if (frame) {
            ++state->stats.inFlightCount;
            state->stats.highWatermark = std::max(state->stats.highWatermark,
                                                  state->stats.inFlightCount);
        }
    }

    discardedFrames.clear();
    if (frame)
        return state->lease(std::move(frame), key, epoch, true);

    auto allocatedFrames = allocateFrames(key, transientAllocation ? 1 : allocationReservation);
    {
        std::lock_guard lock(state->mutex);
        if (allocationReservation > 0) {
            if (state->reservedAllocations >= allocationReservation)
                state->reservedAllocations -= allocationReservation;
            else
                state->reservedAllocations = 0;
        }
        if (allocatedFrames.empty() || state->closed || !state->activeKey.has_value()
            || key != *state->activeKey || epoch != state->formatEpoch) {
            return {};
        }

        frame = std::move(allocatedFrames.front());
        allocatedFrames.erase(allocatedFrames.begin());
        if (transientAllocation) {
            ++state->stats.transientAllocationCount;
        } else {
            const std::size_t retainedAllocations = allocatedFrames.size() + 1;
            state->stats.allocationCount += retainedAllocations;
            state->stats.retainedCount += retainedAllocations;
            for (auto& idleFrame : allocatedFrames)
                state->freeFrames.push_back(std::move(idleFrame));
            allocatedFrames.clear();
        }
        ++state->stats.inFlightCount;
        state->stats.highWatermark = std::max(state->stats.highWatermark,
                                              state->stats.inFlightCount);
    }
    return state->lease(std::move(frame), key, epoch, !transientAllocation);
}

void CpuVideoPicturePool::close()
{
    const auto state = m_state;
    std::vector<AVFramePtr> idleFrames;
    {
        std::lock_guard lock(state->mutex);
        if (state->closed)
            return;

        state->closed = true;
        ++state->formatEpoch;
        state->activeKey.reset();
        const std::size_t idleCount = state->freeFrames.size();
        idleFrames = std::move(state->freeFrames);
        if (state->stats.retainedCount >= idleCount)
            state->stats.retainedCount -= idleCount;
        else
            state->stats.retainedCount = 0;
    }
}

void CpuVideoPicturePool::reset()
{
    const auto state = m_state;
    std::vector<AVFramePtr> idleFrames;
    {
        std::lock_guard lock(state->mutex);
        ++state->formatEpoch;
        state->activeKey.reset();
        const std::size_t idleCount = state->freeFrames.size();
        idleFrames = std::move(state->freeFrames);
        if (state->stats.retainedCount >= idleCount)
            state->stats.retainedCount -= idleCount;
        else
            state->stats.retainedCount = 0;

        const auto retainedInFlight = state->stats.retainedCount;
        const auto totalInFlight = state->stats.inFlightCount;
        state->stats = {
            .retainedCount = retainedInFlight,
            .inFlightCount = totalInFlight,
        };
        state->reservedAllocations = 0;
        state->closed = false;
    }
}

VideoPicturePoolStats CpuVideoPicturePool::stats() const
{
    const auto state = m_state;
    std::lock_guard lock(state->mutex);
    return state->stats;
}

} // namespace media_sdk
