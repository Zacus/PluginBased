#pragma once

#include <memory>
#include <string>
#include <stdexcept>

// FFmpeg C 库必须用 extern "C" 包裹
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include <memory>
#include <stdexcept>

// ── RAII 删除器 ───────────────────────────────────────────────────────────────
// 所有 FFmpeg 资源通过 unique_ptr + 自定义删除器管理，
// 不需要手写 avformat_close_input / av_frame_free 等，也不会忘记释放。

struct AVFormatContextDeleter {
    void operator()(AVFormatContext* ctx) const {
        if (ctx) avformat_close_input(&ctx);
    }
};

struct AVCodecContextDeleter {
    void operator()(AVCodecContext* ctx) const {
        if (ctx) avcodec_free_context(&ctx);
    }
};

struct AVFrameDeleter {
    void operator()(AVFrame* f) const {
        if (f) av_frame_free(&f);
    }
};

struct AVPacketDeleter {
    void operator()(AVPacket* p) const {
        if (p) av_packet_free(&p);
    }
};

struct SwsContextDeleter {
    void operator()(SwsContext* ctx) const {
        if (ctx) sws_freeContext(ctx);
    }
};

struct SwrContextDeleter {
    void operator()(SwrContext* ctx) const {
        if (ctx) swr_free(&ctx);
    }
};

// ── 类型别名 ──────────────────────────────────────────────────────────────────
using AVFormatContextPtr = std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;
using AVCodecContextPtr  = std::unique_ptr<AVCodecContext,  AVCodecContextDeleter>;
using AVFramePtr         = std::unique_ptr<AVFrame,         AVFrameDeleter>;
using AVPacketPtr        = std::unique_ptr<AVPacket,        AVPacketDeleter>;
using SwsContextPtr      = std::unique_ptr<SwsContext,      SwsContextDeleter>;
using SwrContextPtr      = std::unique_ptr<SwrContext,      SwrContextDeleter>;

// ── 工厂函数 ──────────────────────────────────────────────────────────────────
inline AVFramePtr  make_frame()  { return AVFramePtr(av_frame_alloc()); }
inline AVPacketPtr make_packet() { return AVPacketPtr(av_packet_alloc()); }

// ── 错误码转字符串 ────────────────────────────────────────────────────────────
inline std::string av_err(int errcode)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(errcode, buf, sizeof(buf));
    return buf;
}
