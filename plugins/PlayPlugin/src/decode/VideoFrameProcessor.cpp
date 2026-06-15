#include "decode/VideoFrameProcessor.h"

// Implements hardware frame transfer and pixel-format normalization.
// Keeping this in one processor leaves FFmpegDecoder focused on packet flow and queueing.

#include "Logger.h"
#include "hw/HardwareDecoderBackend.h"

#include <QElapsedTimer>

#include <utility>

namespace {

constexpr int MaxHardwareTransferFailureLogs = 3;

bool isRendererSupportedVideoFormat(int format)
{
    switch (format)
    {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_NV12:
    case AV_PIX_FMT_YUV420P10LE:
    case AV_PIX_FMT_P010LE:
    case AV_PIX_FMT_YUV422P10LE:
    case AV_PIX_FMT_YUV444P10LE:
        return true;
    default:
        return false;
    }
}

} // namespace

void VideoFrameProcessor::reset()
{
    m_videoSwsCtx.reset();
    m_hardwareTransferFailureCount = 0;
}

AVFramePtr VideoFrameProcessor::prepareForQueue(AVFramePtr frame,
                                                HardwareDecoderBackend* hardwareDecoder,
                                                bool directRenderingEnabled,
                                                DecodePerformanceStats& stats)
{
    if (shouldPreserveHardwareFrameForDirectRender(frame.get(), hardwareDecoder, directRenderingEnabled))
    {
        ++stats.nativeVideoFrames;
        return frame;
    }

    if (hardwareDecoder && hardwareDecoder->isHardwareFrame(frame.get()))
        ++stats.nativeFallbackVideoFrames;

    frame = transferHardwareFrameToCpu(std::move(frame), hardwareDecoder, stats);
    if (!frame)
        return {};
    return normalizeVideoFrame(std::move(frame), stats);
}

bool VideoFrameProcessor::shouldPreserveHardwareFrameForDirectRender(
    const AVFrame* frame,
    HardwareDecoderBackend* hardwareDecoder,
    bool directRenderingEnabled) const
{
    return directRenderingEnabled &&
           hardwareDecoder &&
           frame &&
           frame->format == AV_PIX_FMT_VIDEOTOOLBOX;
}

AVFramePtr VideoFrameProcessor::transferHardwareFrameToCpu(AVFramePtr frame,
                                                           HardwareDecoderBackend* hardwareDecoder,
                                                           DecodePerformanceStats& stats)
{
    if (!hardwareDecoder || !hardwareDecoder->isHardwareFrame(frame.get()))
        return frame;

    ++stats.hardwareVideoFrames;
    QElapsedTimer transferTimer;
    transferTimer.start();
    AVFramePtr cpuFrame = hardwareDecoder->transferToCpuFrame(frame.get());
    const qint64 transferUs = transferTimer.nsecsElapsed() / 1000;
    stats.transferTotalUs += transferUs;
    if (transferUs > stats.transferMaxUs)
        stats.transferMaxUs = transferUs;

    if (!cpuFrame)
    {
        ++stats.transferFailures;
        ++m_hardwareTransferFailureCount;
        if (m_hardwareTransferFailureCount <= MaxHardwareTransferFailureLogs)
        {
            LOG_WARN("FFmpegDecoder: hardware frame transfer failed, dropping frame");
        }
        return {};
    }

    m_hardwareTransferFailureCount = 0;
    ++stats.transferredVideoFrames;
    stats.cpuPixelFormat = cpuFrame->format;
    copyFrameMetadata(frame.get(), cpuFrame.get());
    return cpuFrame;
}

void VideoFrameProcessor::copyFrameMetadata(const AVFrame* source, AVFrame* destination) const
{
    destination->pts = source->pts;
    destination->sample_aspect_ratio = source->sample_aspect_ratio;
    destination->color_range = source->color_range;
    destination->colorspace = source->colorspace;
    destination->color_primaries = source->color_primaries;
    destination->color_trc = source->color_trc;
    destination->chroma_location = source->chroma_location;
}

AVFramePtr VideoFrameProcessor::normalizeVideoFrame(AVFramePtr frame, DecodePerformanceStats& stats)
{
    if (!frame || isRendererSupportedVideoFormat(frame->format))
        return frame;

    QElapsedTimer normalizeTimer;
    normalizeTimer.start();

    auto converted = make_frame();
    converted->format = AV_PIX_FMT_YUV420P;
    converted->width = frame->width;
    converted->height = frame->height;
    converted->pts = frame->pts;
    converted->sample_aspect_ratio = frame->sample_aspect_ratio;
    converted->color_range = frame->color_range;
    converted->colorspace = frame->colorspace;
    converted->color_primaries = frame->color_primaries;
    converted->color_trc = frame->color_trc;
    converted->chroma_location = frame->chroma_location;

    int ret = av_frame_get_buffer(converted.get(), 32);
    if (ret < 0)
    {
        LOG_WARN("FFmpegDecoder: av_frame_get_buffer for pixel format fallback failed: {}",
                 av_err(ret));
        return {};
    }

    ret = av_frame_make_writable(converted.get());
    if (ret < 0)
    {
        LOG_WARN("FFmpegDecoder: av_frame_make_writable for pixel format fallback failed: {}",
                 av_err(ret));
        return {};
    }

    SwsContext* oldCtx = m_videoSwsCtx.release();
    SwsContext* sws = sws_getCachedContext(
        oldCtx,
        frame->width,
        frame->height,
        static_cast<AVPixelFormat>(frame->format),
        converted->width,
        converted->height,
        AV_PIX_FMT_YUV420P,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr);

    if (!sws)
    {
        LOG_WARN("FFmpegDecoder: sws_getCachedContext failed for pixel format {}",
                 frame->format);
        return {};
    }
    m_videoSwsCtx.reset(sws);

    ret = sws_scale(m_videoSwsCtx.get(),
                    frame->data,
                    frame->linesize,
                    0,
                    frame->height,
                    converted->data,
                    converted->linesize);
    if (ret <= 0)
    {
        LOG_WARN("FFmpegDecoder: sws_scale failed for pixel format fallback");
        return {};
    }

    const qint64 normalizeUs = normalizeTimer.nsecsElapsed() / 1000;
    ++stats.normalizedVideoFrames;
    stats.normalizeTotalUs += normalizeUs;
    if (normalizeUs > stats.normalizeMaxUs)
        stats.normalizeMaxUs = normalizeUs;
    stats.cpuPixelFormat = converted->format;

    return converted;
}
