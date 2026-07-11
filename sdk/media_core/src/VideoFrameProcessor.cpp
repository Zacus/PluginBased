#include "VideoFrameProcessor.h"

#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

namespace media_sdk {
namespace {

Result<VideoFrame> processingFailure(std::string detail)
{
    return Result<VideoFrame>::failure({
        MediaErrorCode::DecodeFailed,
        "Failed to process video frame",
        std::move(detail),
    });
}

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

PixelFormat mapPixelFormat(int format)
{
    switch (format)
    {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
        return PixelFormat::Yuv420P;
    case AV_PIX_FMT_NV12:
        return PixelFormat::Nv12;
    case AV_PIX_FMT_P010LE:
        return PixelFormat::P010;
    case AV_PIX_FMT_YUV420P10LE:
        return PixelFormat::Yuv420P10;
    case AV_PIX_FMT_YUV422P10LE:
        return PixelFormat::Yuv422P10;
    case AV_PIX_FMT_YUV444P10LE:
        return PixelFormat::Yuv444P10;
    default:
        return PixelFormat::Unknown;
    }
}

ColorRange mapColorRange(AVColorRange range)
{
    switch (range)
    {
    case AVCOL_RANGE_JPEG:
        return ColorRange::Full;
    case AVCOL_RANGE_MPEG:
        return ColorRange::Limited;
    default:
        return ColorRange::Unknown;
    }
}

ColorSpace mapColorSpace(AVColorSpace colorSpace)
{
    switch (colorSpace)
    {
    case AVCOL_SPC_BT709:
        return ColorSpace::Bt709;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
        return ColorSpace::Bt601;
    default:
        return ColorSpace::Unknown;
    }
}

std::chrono::microseconds framePts(const AVFrame* frame)
{
    if (!frame || frame->pts == AV_NOPTS_VALUE)
        return std::chrono::microseconds { 0 };
    return std::chrono::microseconds { frame->pts };
}

int halfRoundUp(int value)
{
    return (value + 1) / 2;
}

std::vector<PlaneView> planeViews(const AVFrame* frame)
{
    if (!frame)
        return {};

    std::vector<PlaneView> planes;
    switch (frame->format)
    {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUV420P10LE:
        planes.push_back({ reinterpret_cast<const std::byte*>(frame->data[0]),
                           frame->linesize[0],
                           frame->width,
                           frame->height });
        planes.push_back({ reinterpret_cast<const std::byte*>(frame->data[1]),
                           frame->linesize[1],
                           halfRoundUp(frame->width),
                           halfRoundUp(frame->height) });
        planes.push_back({ reinterpret_cast<const std::byte*>(frame->data[2]),
                           frame->linesize[2],
                           halfRoundUp(frame->width),
                           halfRoundUp(frame->height) });
        break;
    case AV_PIX_FMT_NV12:
    case AV_PIX_FMT_P010LE:
        planes.push_back({ reinterpret_cast<const std::byte*>(frame->data[0]),
                           frame->linesize[0],
                           frame->width,
                           frame->height });
        planes.push_back({ reinterpret_cast<const std::byte*>(frame->data[1]),
                           frame->linesize[1],
                           frame->width,
                           halfRoundUp(frame->height) });
        break;
    case AV_PIX_FMT_YUV422P10LE:
        planes.push_back({ reinterpret_cast<const std::byte*>(frame->data[0]),
                           frame->linesize[0],
                           frame->width,
                           frame->height });
        planes.push_back({ reinterpret_cast<const std::byte*>(frame->data[1]),
                           frame->linesize[1],
                           halfRoundUp(frame->width),
                           frame->height });
        planes.push_back({ reinterpret_cast<const std::byte*>(frame->data[2]),
                           frame->linesize[2],
                           halfRoundUp(frame->width),
                           frame->height });
        break;
    case AV_PIX_FMT_YUV444P10LE:
        planes.push_back({ reinterpret_cast<const std::byte*>(frame->data[0]),
                           frame->linesize[0],
                           frame->width,
                           frame->height });
        planes.push_back({ reinterpret_cast<const std::byte*>(frame->data[1]),
                           frame->linesize[1],
                           frame->width,
                           frame->height });
        planes.push_back({ reinterpret_cast<const std::byte*>(frame->data[2]),
                           frame->linesize[2],
                           frame->width,
                           frame->height });
        break;
    default:
        break;
    }

    return planes;
}

std::shared_ptr<void> shareFrameStorage(AVFramePtr frame, AVFrame*& rawFrame)
{
    rawFrame = frame.release();
    std::shared_ptr<AVFrame> sharedFrame(rawFrame, [](AVFrame* releasedFrame) {
        if (releasedFrame)
            av_frame_free(&releasedFrame);
    });
    return sharedFrame;
}

} // namespace

void VideoFrameProcessor::reset()
{
    m_videoSwsContext.reset();
}

Result<VideoFrame> VideoFrameProcessor::process(AVFramePtr frame,
                                                VideoFrameProcessOptions options,
                                                HardwareDecoderBackend* hardwareDecoder,
                                                DecodePerformanceStats* stats)
{
    if (!frame)
        return processingFailure("Cannot process a null AVFrame");

    if (stats && stats->sourcePixelFormat == AV_PIX_FMT_NONE)
        stats->sourcePixelFormat = frame->format;

    if (frame->format == AV_PIX_FMT_VIDEOTOOLBOX && options.preferNativeVideoFrames)
        return createNativeVideoFrame(std::move(frame), stats);

    frame = transferHardwareFrameToCpu(std::move(frame), hardwareDecoder, stats);
    if (!frame)
        return processingFailure("Failed to transfer hardware frame to CPU memory");

    frame = normalizeVideoFrame(std::move(frame), stats);
    if (!frame)
        return processingFailure("Failed to normalize video frame pixel format");

    return createVideoFrame(std::move(frame), stats);
}

AVFramePtr VideoFrameProcessor::transferHardwareFrameToCpu(AVFramePtr frame,
                                                           HardwareDecoderBackend* hardwareDecoder,
                                                           DecodePerformanceStats* stats)
{
    if (!hardwareDecoder || !hardwareDecoder->isHardwareFrame(frame.get()))
        return frame;

    if (stats)
        ++stats->hardwareVideoFrames;

    const auto started = std::chrono::steady_clock::now();
    AVFramePtr cpuFrame = hardwareDecoder->transferToCpuFrame(frame.get());
    const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started).count();

    if (stats)
    {
        stats->transferTotalUs += elapsedUs;
        if (elapsedUs > stats->transferMaxUs)
            stats->transferMaxUs = elapsedUs;
    }

    if (!cpuFrame)
    {
        if (stats)
            ++stats->transferFailures;
        return {};
    }

    if (stats)
    {
        ++stats->transferredVideoFrames;
        stats->cpuPixelFormat = cpuFrame->format;
    }
    copyFrameMetadata(frame.get(), cpuFrame.get());
    return cpuFrame;
}

AVFramePtr VideoFrameProcessor::normalizeVideoFrame(AVFramePtr frame, DecodePerformanceStats* stats)
{
    if (!frame || isRendererSupportedVideoFormat(frame->format))
        return frame;

    const auto started = std::chrono::steady_clock::now();
    auto converted = makeFrame();
    if (!converted)
        return {};
    if (stats)
        ++stats->normalizedFrameHeaderAllocations;

    converted->format = AV_PIX_FMT_YUV420P;
    converted->width = frame->width;
    converted->height = frame->height;
    copyFrameMetadata(frame.get(), converted.get());

    int ret = av_frame_get_buffer(converted.get(), 32);
    if (ret < 0)
        return {};
    if (stats)
        ++stats->normalizedPixelBufferAllocations;

    ret = av_frame_make_writable(converted.get());
    if (ret < 0)
        return {};

    SwsContext* oldContext = m_videoSwsContext.release();
    SwsContext* sws = sws_getCachedContext(
        oldContext,
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
        m_videoSwsContext.reset(oldContext);
        return {};
    }
    m_videoSwsContext.reset(sws);

    ret = sws_scale(m_videoSwsContext.get(),
                    frame->data,
                    frame->linesize,
                    0,
                    frame->height,
                    converted->data,
                    converted->linesize);
    if (ret <= 0)
        return {};

    if (stats)
    {
        const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        ++stats->normalizedVideoFrames;
        stats->normalizeTotalUs += elapsedUs;
        if (elapsedUs > stats->normalizeMaxUs)
            stats->normalizeMaxUs = elapsedUs;
        stats->cpuPixelFormat = converted->format;
    }

    return converted;
}

Result<VideoFrame> VideoFrameProcessor::createVideoFrame(AVFramePtr frame,
                                                         DecodePerformanceStats* stats) const
{
    if (!frame)
        return processingFailure("Cannot create VideoFrame from null AVFrame");

    AVFrame* rawFrame = nullptr;
    std::shared_ptr<void> storage = shareFrameStorage(std::move(frame), rawFrame);
    const auto planes = planeViews(rawFrame);

    return Result<VideoFrame>::success(VideoFrame({
        .width = rawFrame->width,
        .height = rawFrame->height,
        .pixelFormat = mapPixelFormat(rawFrame->format),
        .colorRange = mapColorRange(rawFrame->color_range),
        .colorSpace = mapColorSpace(rawFrame->colorspace),
        .pts = framePts(rawFrame),
        .planes = planes,
        .nativeHandle = {},
        .storage = std::move(storage),
    }));
}

Result<VideoFrame> VideoFrameProcessor::createNativeVideoFrame(AVFramePtr frame,
                                                               DecodePerformanceStats* stats) const
{
    if (!frame)
        return processingFailure("Cannot create native VideoFrame from null AVFrame");

    if (stats)
        ++stats->nativeVideoFrames;

    AVFrame* rawFrame = nullptr;
    std::shared_ptr<void> storage = shareFrameStorage(std::move(frame), rawFrame);
    NativeHandle handle {
        .kind = NativeHandleKind::VideoToolboxPixelBuffer,
        .handle = rawFrame->data[3],
        .pixelFormat = rawFrame->format,
    };

    return Result<VideoFrame>::success(VideoFrame({
        .width = rawFrame->width,
        .height = rawFrame->height,
        .pixelFormat = PixelFormat::Native,
        .colorRange = mapColorRange(rawFrame->color_range),
        .colorSpace = mapColorSpace(rawFrame->colorspace),
        .pts = framePts(rawFrame),
        .planes = {},
        .nativeHandle = handle,
        .storage = std::move(storage),
    }));
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

} // namespace media_sdk
