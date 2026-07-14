#include "ExperimentalDecoderBufferPool.h"

#include <algorithm>
#include <array>
#include <climits>
#include <limits>
#include <utility>

extern "C" {
#include <libavutil/cpu.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
}

namespace media_benchmark {

ExperimentalDecoderBufferPool::PoolSet::~PoolSet()
{
    clear();
}

ExperimentalDecoderBufferPool::PoolSet::PoolSet(PoolSet&& other) noexcept
    : key(other.key)
    , pools(std::exchange(other.pools, {}))
    , linesizes(other.linesizes)
    , valid(std::exchange(other.valid, false))
{
}

ExperimentalDecoderBufferPool::PoolSet&
ExperimentalDecoderBufferPool::PoolSet::operator=(PoolSet&& other) noexcept
{
    if (this == &other)
        return *this;
    clear();
    key = other.key;
    pools = std::exchange(other.pools, {});
    linesizes = other.linesizes;
    valid = std::exchange(other.valid, false);
    return *this;
}

void ExperimentalDecoderBufferPool::PoolSet::clear()
{
    for (auto& pool : pools)
        av_buffer_pool_uninit(&pool);
    valid = false;
}

ExperimentalDecoderBufferPool::ExperimentalDecoderBufferPool(bool prototypeEnabled)
    : m_prototypeEnabled(prototypeEnabled)
{
}

ExperimentalDecoderBufferPool::~ExperimentalDecoderBufferPool()
{
    if (m_attachedContext)
        detach(m_attachedContext);
    close();
}

bool ExperimentalDecoderBufferPool::attach(AVCodecContext* context)
{
    if (!context)
        return false;

    std::lock_guard lock(m_mutex);
    if (m_attachedContext)
        return false;
    m_attachedContext = context;
    m_previousGetBuffer2 = context->get_buffer2
        ? context->get_buffer2
        : avcodec_default_get_buffer2;
    m_previousOpaque = context->opaque;
    context->opaque = this;
    context->get_buffer2 = &ExperimentalDecoderBufferPool::getBufferCallback;
    return true;
}

void ExperimentalDecoderBufferPool::detach(AVCodecContext* context)
{
    std::lock_guard lock(m_mutex);
    if (!context || context != m_attachedContext)
        return;
    if (context->get_buffer2 == &ExperimentalDecoderBufferPool::getBufferCallback
        && context->opaque == this) {
        context->get_buffer2 = m_previousGetBuffer2;
        context->opaque = m_previousOpaque;
    }
    m_attachedContext = nullptr;
    m_previousGetBuffer2 = nullptr;
    m_previousOpaque = nullptr;
}

void ExperimentalDecoderBufferPool::close()
{
    std::lock_guard lock(m_mutex);
    m_activePools.clear();
}

ExperimentalDecoderBufferPoolStats ExperimentalDecoderBufferPool::stats() const
{
    return {
        .callbackCount = m_callbackCount.load(),
        .prototypeFrameCount = m_prototypeFrameCount.load(),
        .fallbackCount = m_fallbackCount.load(),
        .poolRebuildCount = m_poolRebuildCount.load(),
        .planeAcquireCount = m_planeAcquireCount.load(),
        .planeAllocationCount = m_planeAllocationCount.load(),
    };
}

int ExperimentalDecoderBufferPool::getBufferCallback(
    AVCodecContext* context,
    AVFrame* frame,
    int flags) noexcept
{
    auto* pool = context ? static_cast<ExperimentalDecoderBufferPool*>(context->opaque) : nullptr;
    if (!pool)
        return avcodec_default_get_buffer2(context, frame, flags);
    const int format = frame ? frame->format : AV_PIX_FMT_NONE;
    const int width = frame ? frame->width : 0;
    const int height = frame ? frame->height : 0;
    try {
        return pool->getBuffer(context, frame, flags);
    } catch (...) {
        if (frame) {
            av_frame_unref(frame);
            frame->format = format;
            frame->width = width;
            frame->height = height;
        }
        return pool->fallback(context, frame, flags);
    }
}

AVBufferRef* ExperimentalDecoderBufferPool::allocatePlane(void* opaque, std::size_t size) noexcept
{
    auto* pool = static_cast<ExperimentalDecoderBufferPool*>(opaque);
    AVBufferRef* buffer = av_buffer_allocz(size);
    if (pool && buffer)
        ++pool->m_planeAllocationCount;
    return buffer;
}

int ExperimentalDecoderBufferPool::getBuffer(
    AVCodecContext* context,
    AVFrame* frame,
    int flags)
{
    ++m_callbackCount;
    if (!m_prototypeEnabled || !eligible(context, frame))
        return fallback(context, frame, flags);

    std::lock_guard lock(m_mutex);
    const FormatKey requested {
        .format = static_cast<AVPixelFormat>(frame->format),
        .width = frame->width,
        .height = frame->height,
    };
    if (!m_activePools.valid || !(m_activePools.key == requested)) {
        PoolSet replacement;
        if (!rebuildPools(context, frame, replacement))
            return fallback(context, frame, flags);
        m_activePools = std::move(replacement);
        ++m_poolRebuildCount;
    }

    frame->extended_data = frame->data;
    for (std::size_t index = 0; index < m_activePools.pools.size(); ++index) {
        if (!m_activePools.pools[index]) {
            frame->data[index] = nullptr;
            frame->linesize[index] = 0;
            continue;
        }
        frame->buf[index] = av_buffer_pool_get(m_activePools.pools[index]);
        if (!frame->buf[index]) {
            const int format = frame->format;
            const int width = frame->width;
            const int height = frame->height;
            av_frame_unref(frame);
            frame->format = format;
            frame->width = width;
            frame->height = height;
            return fallback(context, frame, flags);
        }
        ++m_planeAcquireCount;
        frame->data[index] = frame->buf[index]->data;
        frame->linesize[index] = m_activePools.linesizes[index];
    }
    for (std::size_t index = m_activePools.pools.size(); index < AV_NUM_DATA_POINTERS; ++index) {
        frame->data[index] = nullptr;
        frame->linesize[index] = 0;
    }
    ++m_prototypeFrameCount;
    return 0;
}

int ExperimentalDecoderBufferPool::fallback(
    AVCodecContext* context,
    AVFrame* frame,
    int flags)
{
    ++m_fallbackCount;
    const auto callback = m_previousGetBuffer2
        ? m_previousGetBuffer2
        : avcodec_default_get_buffer2;
    return callback(context, frame, flags);
}

bool ExperimentalDecoderBufferPool::eligible(
    const AVCodecContext* context,
    const AVFrame* frame) const
{
    if (!context || !frame || context->codec_type != AVMEDIA_TYPE_VIDEO
        || !context->codec || !(context->codec->capabilities & AV_CODEC_CAP_DR1)
        || (context->codec_id != AV_CODEC_ID_H264 && context->codec_id != AV_CODEC_ID_HEVC)
        || context->hw_device_ctx || context->hw_frames_ctx || frame->format == AV_PIX_FMT_NONE
        || frame->width <= 0 || frame->height <= 0) {
        return false;
    }
    for (int index = 0; index < 4; ++index) {
        if (frame->data[index] || frame->buf[index])
            return false;
    }
    const auto* descriptor = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
    return descriptor && !(descriptor->flags & (AV_PIX_FMT_FLAG_HWACCEL | AV_PIX_FMT_FLAG_BITSTREAM));
}

bool ExperimentalDecoderBufferPool::rebuildPools(
    AVCodecContext* context,
    const AVFrame* frame,
    PoolSet& replacement)
{
    int alignedWidth = frame->width;
    int alignedHeight = frame->height;
    std::array<int, AV_NUM_DATA_POINTERS> strideAlignment {};
    avcodec_align_dimensions2(
        context,
        &alignedWidth,
        &alignedHeight,
        strideAlignment.data());
    if (alignedWidth <= 0 || alignedHeight <= 0)
        return false;

    std::array<int, 4> linesizes {};
    bool aligned = false;
    for (int attempt = 0; attempt < 32; ++attempt) {
        if (av_image_fill_linesizes(
                linesizes.data(),
                static_cast<AVPixelFormat>(frame->format),
                alignedWidth) < 0) {
            return false;
        }
        aligned = true;
        for (std::size_t index = 0; index < linesizes.size(); ++index) {
            const int required = std::max(strideAlignment[index], 1);
            if (linesizes[index] % required != 0) {
                aligned = false;
                break;
            }
        }
        if (aligned)
            break;
        const int increment = alignedWidth & -alignedWidth;
        if (increment <= 0 || alignedWidth > INT_MAX - increment)
            return false;
        alignedWidth += increment;
    }
    if (!aligned)
        return false;

    std::array<std::ptrdiff_t, 4> planeLinesizes {};
    std::copy(linesizes.begin(), linesizes.end(), planeLinesizes.begin());
    std::array<std::size_t, 4> planeSizes {};
    if (av_image_fill_plane_sizes(
            planeSizes.data(),
            static_cast<AVPixelFormat>(frame->format),
            alignedHeight,
            planeLinesizes.data()) < 0) {
        return false;
    }

    const std::size_t maximumAlignment = std::max<std::size_t>(av_cpu_max_align(), 1);
    constexpr std::size_t edgePadding = 16;
    for (std::size_t index = 0; index < planeSizes.size(); ++index) {
        if (planeSizes[index] == 0)
            continue;
        const std::size_t extra = edgePadding + maximumAlignment - 1;
        if (planeSizes[index] > std::numeric_limits<std::size_t>::max() - extra)
            return false;
        replacement.pools[index] = av_buffer_pool_init2(
            planeSizes[index] + extra,
            this,
            &ExperimentalDecoderBufferPool::allocatePlane,
            nullptr);
        if (!replacement.pools[index])
            return false;
        replacement.linesizes[index] = linesizes[index];
    }

    if (!replacement.pools[0])
        return false;
    replacement.key = {
        .format = static_cast<AVPixelFormat>(frame->format),
        .width = frame->width,
        .height = frame->height,
    };
    replacement.valid = true;
    return true;
}

} // namespace media_benchmark
