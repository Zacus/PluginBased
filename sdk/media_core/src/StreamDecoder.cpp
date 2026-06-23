#include "StreamDecoder.h"

#include <cstdint>
#include <utility>

namespace media_sdk {
namespace {

Result<void> decodeFailure(std::string detail)
{
    return Result<void>::failure({
        MediaErrorCode::DecodeFailed,
        "Failed to decode stream frame",
        std::move(detail),
    });
}

} // namespace

Result<void> StreamDecoder::sendPacket(AVCodecContext* codecContext,
                                       const AVPacket* packet,
                                       AVRational timeBase,
                                       const FrameHandler& onFrame) const
{
    const int ret = avcodec_send_packet(codecContext, packet);
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
        return decodeFailure(avErrorString(ret));

    return receiveFrames(codecContext, timeBase, onFrame);
}

Result<void> StreamDecoder::flush(AVCodecContext* codecContext,
                                  AVRational timeBase,
                                  const FrameHandler& onFrame) const
{
    const int ret = avcodec_send_packet(codecContext, nullptr);
    if (ret < 0 && ret != AVERROR_EOF)
        return decodeFailure(avErrorString(ret));

    return receiveFrames(codecContext, timeBase, onFrame);
}

Result<void> StreamDecoder::receiveFrames(AVCodecContext* codecContext,
                                          AVRational timeBase,
                                          const FrameHandler& onFrame) const
{
    while (true)
    {
        auto frame = makeFrame();
        const int ret = avcodec_receive_frame(codecContext, frame.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0)
            return decodeFailure(avErrorString(ret));

        normalizeFramePts(frame.get(), timeBase);
        if (!onFrame(std::move(frame)))
            return Result<void>::failure({
                MediaErrorCode::DecodeFailed,
                "Frame handler rejected decoded frame",
                {},
            });
    }

    return Result<void>::success();
}

void StreamDecoder::normalizeFramePts(AVFrame* frame, AVRational timeBase) const
{
    std::int64_t pts = frame->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE)
        pts = frame->pts;

    if (pts != AV_NOPTS_VALUE && timeBase.den > 0)
        frame->pts = av_rescale_q(pts, timeBase, AVRational { 1, AV_TIME_BASE });
    else
        frame->pts = AV_NOPTS_VALUE;
}

} // namespace media_sdk
