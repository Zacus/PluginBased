#include "decode/StreamFrameDecoder.h"

// Implements packet send, frame receive, and timestamp normalization.
// Queueing policy remains in FFmpegDecoder while raw codec iteration stays here.

#include "Logger.h"

#include <utility>

bool StreamFrameDecoder::sendPacket(AVCodecContext* codecContext,
                                    AVPacket* packet,
                                    AVRational timeBase,
                                    const FrameHandler& onFrame) const
{
    int ret = avcodec_send_packet(codecContext, packet);
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
    {
        LOG_WARN("FFmpegDecoder: avcodec_send_packet: {}", av_err(ret));
        return false;
    }

    while (true)
    {
        auto frame = make_frame();
        ret = avcodec_receive_frame(codecContext, frame.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0)
        {
            LOG_WARN("FFmpegDecoder: avcodec_receive_frame: {}", av_err(ret));
            return false;
        }

        normalizeFramePts(frame.get(), timeBase);
        if (!onFrame(std::move(frame)))
            return false;
    }

    return true;
}

void StreamFrameDecoder::normalizeFramePts(AVFrame* frame, AVRational timeBase) const
{
    qint64 pts = frame->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE)
        pts = frame->pts;
    if (pts != AV_NOPTS_VALUE && timeBase.den > 0)
        frame->pts = av_rescale_q(pts, timeBase, {1, AV_TIME_BASE});
    else
        frame->pts = AV_NOPTS_VALUE;
}
