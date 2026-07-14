#include "DecoderBufferPool.h"

#include <algorithm>
#include <array>
#include <climits>
#include <limits>
#include <utility>

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}

namespace media_sdk {

DecoderBufferPool::PoolSet::~PoolSet()
{
    clear();
}

DecoderBufferPool::PoolSet::PoolSet(PoolSet&& other) noexcept
    : key(other.key)
    , pools(std::exchange(other.pools, {}))
    , linesizes(other.linesizes)
    , valid(std::exchange(other.valid, false))
{
}

DecoderBufferPool::PoolSet& DecoderBufferPool::PoolSet::operator=(PoolSet&& other) noexcept
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

void DecoderBufferPool::PoolSet::clear()
{
    for (auto& pool : pools)
        av_buffer_pool_uninit(&pool);
    valid = false;
}

DecoderBufferPool::~DecoderBufferPool()
{
    if (m_attachedContext)
        detach(m_attachedContext);
    close();
}

bool DecoderBufferPool::attach(AVCodecContext* context)
{
    if (!supportsContext(context))
        return false;

    std::lock_guard lock(m_mutex);
    if (m_attachedContext || context->opaque
        || (context->get_buffer2 && context->get_buffer2 != avcodec_default_get_buffer2)) {
        return false;
    }

    m_attachedContext = context;
    m_previousGetBuffer2 = context->get_buffer2
        ? context->get_buffer2
        : avcodec_default_get_buffer2;
    context->opaque = this;
    context->get_buffer2 = &DecoderBufferPool::getBufferCallback;
    return true;
}

void DecoderBufferPool::detach(AVCodecContext* context)
{
    std::lock_guard lock(m_mutex);
    if (!context || context != m_attachedContext)
        return;

    if (context->get_buffer2 == &DecoderBufferPool::getBufferCallback
        && context->opaque == this) {
        context->get_buffer2 = m_previousGetBuffer2;
        context->opaque = nullptr;
    }
    m_attachedContext = nullptr;
    m_previousGetBuffer2 = nullptr;
}

void DecoderBufferPool::close()
{
    std::lock_guard lock(m_mutex);
    m_activePools.clear();
}

DecoderBufferPoolStats DecoderBufferPool::stats() const
{
    return {
        .callbackCount = m_callbackCount.load(),
        .pooledFrameCount = m_pooledFrameCount.load(),
        .fallbackCount = m_fallbackCount.load(),
        .poolRebuildCount = m_poolRebuildCount.load(),
        .planeAcquireCount = m_planeAcquireCount.load(),
        .planeAllocationCount = m_planeAllocationCount.load(),
    };
}

int DecoderBufferPool::getBufferCallback(
    AVCodecContext* context,
    AVFrame* frame,
    int flags) noexcept
{
    auto* pool = context ? static_cast<DecoderBufferPool*>(context->opaque) : nullptr;
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

AVBufferRef* DecoderBufferPool::allocatePlane(void* opaque, std::size_t size) noexcept
{
    auto* pool = static_cast<DecoderBufferPool*>(opaque);
    AVBufferRef* buffer = av_buffer_allocz(size);
    if (pool && buffer)
        ++pool->m_planeAllocationCount;
    return buffer;
}

int DecoderBufferPool::getBuffer(
    AVCodecContext* context,
    AVFrame* frame,
    int flags)
{
    ++m_callbackCount;
    if (!supportsFrame(context, frame))
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
    ++m_pooledFrameCount;
    return 0;
}

int DecoderBufferPool::fallback(
    AVCodecContext* context,
    AVFrame* frame,
    int flags) noexcept
{
    ++m_fallbackCount;
    const auto callback = m_previousGetBuffer2
        ? m_previousGetBuffer2
        : avcodec_default_get_buffer2;
    return callback(context, frame, flags);
}

bool DecoderBufferPool::supportsContext(const AVCodecContext* context)
{
    if (!context || context->codec_type != AVMEDIA_TYPE_VIDEO || !context->codec
        || !(context->codec->capabilities & AV_CODEC_CAP_DR1)
        || context->hw_device_ctx || context->hw_frames_ctx) {
        return false;
    }

    return context->codec_id == AV_CODEC_ID_H264
        || context->codec_id == AV_CODEC_ID_HEVC
        || context->codec_id == AV_CODEC_ID_PRORES;
}

bool DecoderBufferPool::supportsFrame(
    const AVCodecContext* context,
    const AVFrame* frame)
{
    if (!supportsContext(context) || !frame || frame->format == AV_PIX_FMT_NONE
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

bool DecoderBufferPool::rebuildPools(
    AVCodecContext* context,
    const AVFrame* frame,
    PoolSet& replacement)
{
    int alignedWidth = frame->width;
    int alignedHeight = frame->height;
    std::array<int, AV_NUM_DATA_POINTERS> strideAlignment {};
    avcodec_align_dimensions2(context, &alignedWidth, &alignedHeight, strideAlignment.data());
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

    for (std::size_t index = 0; index < planeSizes.size(); ++index) {
        if (planeSizes[index] == 0)
            continue;
        if (planeSizes[index] > std::numeric_limits<std::size_t>::max()
                - AV_INPUT_BUFFER_PADDING_SIZE) {
            return false;
        }

        replacement.pools[index] = av_buffer_pool_init2(
            planeSizes[index] + AV_INPUT_BUFFER_PADDING_SIZE,
            this,
            &DecoderBufferPool::allocatePlane,
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

} // namespace media_sdk
