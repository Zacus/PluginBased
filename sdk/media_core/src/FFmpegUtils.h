#pragma once

#include <memory>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/buffer.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace media_sdk {

struct AVFormatContextDeleter {
    void operator()(AVFormatContext* context) const
    {
        if (context)
            avformat_close_input(&context);
    }
};

struct AVCodecContextDeleter {
    void operator()(AVCodecContext* context) const
    {
        if (context)
            avcodec_free_context(&context);
    }
};

struct AVFrameDeleter {
    void operator()(AVFrame* frame) const
    {
        if (frame)
            av_frame_free(&frame);
    }
};

struct AVPacketDeleter {
    void operator()(AVPacket* packet) const
    {
        if (packet)
            av_packet_free(&packet);
    }
};

struct AVBufferRefDeleter {
    void operator()(AVBufferRef* ref) const
    {
        if (ref)
            av_buffer_unref(&ref);
    }
};

struct SwsContextDeleter {
    void operator()(SwsContext* context) const
    {
        if (context)
            sws_freeContext(context);
    }
};

struct SwrContextDeleter {
    void operator()(SwrContext* context) const
    {
        if (context)
            swr_free(&context);
    }
};

using AVFormatContextPtr = std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;
using AVCodecContextPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;
using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;
using AVBufferRefPtr = std::unique_ptr<AVBufferRef, AVBufferRefDeleter>;
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;

inline AVFramePtr makeFrame()
{
    return AVFramePtr(av_frame_alloc());
}

inline AVPacketPtr makePacket()
{
    return AVPacketPtr(av_packet_alloc());
}

inline std::string avErrorString(int errorCode)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(errorCode, buffer, sizeof(buffer));
    return buffer;
}

} // namespace media_sdk
