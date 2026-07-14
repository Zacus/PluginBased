#include "ExperimentalDecoderBufferPool.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>

extern "C" {
#include <libavutil/cpu.h>
}

namespace {

struct CodecContextDeleter {
    void operator()(AVCodecContext* context) const
    {
        avcodec_free_context(&context);
    }
};

struct FrameDeleter {
    void operator()(AVFrame* frame) const
    {
        av_frame_free(&frame);
    }
};

using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

CodecContextPtr makeContext(AVCodecID codecId = AV_CODEC_ID_H264)
{
    const AVCodec* codec = avcodec_find_decoder(codecId);
    assert(codec);
    CodecContextPtr context(avcodec_alloc_context3(codec));
    assert(context);
    context->codec_type = AVMEDIA_TYPE_VIDEO;
    context->codec_id = codecId;
    context->pix_fmt = AV_PIX_FMT_YUV420P;
    context->width = 64;
    context->height = 48;
    return context;
}

FramePtr makeRequestedFrame(int width = 64, int height = 48)
{
    FramePtr frame(av_frame_alloc());
    assert(frame);
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = width;
    frame->height = height;
    return frame;
}

int fallbackBuffer(AVCodecContext*, AVFrame* frame, int)
{
    return av_frame_get_buffer(frame, 32);
}

void assertPlaneCoverageAndAlignment(const AVFrame* frame)
{
    const std::size_t alignment = av_cpu_max_align();
    for (int plane = 0; plane < 3; ++plane) {
        assert(frame->buf[plane]);
        assert(frame->data[plane]);
        assert(frame->linesize[plane] > 0);
        assert(reinterpret_cast<std::uintptr_t>(frame->data[plane]) % alignment == 0);
        assert(frame->data[plane] >= frame->buf[plane]->data);
        assert(frame->data[plane] < frame->buf[plane]->data + frame->buf[plane]->size);
    }
}

void allocatesAlignedPlanesAndReusesReleasedBuffers()
{
    auto context = makeContext();
    media_benchmark::ExperimentalDecoderBufferPool pool(true);
    assert(pool.attach(context.get()));

    auto first = makeRequestedFrame();
    assert(context->get_buffer2(context.get(), first.get(), AV_GET_BUFFER_FLAG_REF) == 0);
    assertPlaneCoverageAndAlignment(first.get());
    std::array<std::uint8_t*, 3> firstData {
        first->data[0], first->data[1], first->data[2]
    };
    const auto firstStats = pool.stats();
    assert(firstStats.prototypeFrameCount == 1);
    assert(firstStats.planeAcquireCount == 3);
    assert(firstStats.planeAllocationCount == 3);

    av_frame_unref(first.get());
    first->format = AV_PIX_FMT_YUV420P;
    first->width = 64;
    first->height = 48;
    assert(context->get_buffer2(context.get(), first.get(), AV_GET_BUFFER_FLAG_REF) == 0);
    assert(first->data[0] == firstData[0]);
    assert(first->data[1] == firstData[1]);
    assert(first->data[2] == firstData[2]);
    assert(pool.stats().planeAllocationCount == 3);

    pool.detach(context.get());
}

void simultaneousFramesNeverSharePlaneStorage()
{
    auto context = makeContext();
    media_benchmark::ExperimentalDecoderBufferPool pool(true);
    assert(pool.attach(context.get()));
    auto first = makeRequestedFrame();
    auto second = makeRequestedFrame();

    assert(context->get_buffer2(context.get(), first.get(), AV_GET_BUFFER_FLAG_REF) == 0);
    assert(context->get_buffer2(context.get(), second.get(), AV_GET_BUFFER_FLAG_REF) == 0);
    for (int plane = 0; plane < 3; ++plane)
        assert(first->data[plane] != second->data[plane]);
    const auto stats = pool.stats();
    assert(stats.planeAcquireCount == 6);
    assert(stats.planeAllocationCount == 6);

    pool.detach(context.get());
}

void prores422TenBitUsesPrototypeBuffers()
{
    auto context = makeContext(AV_CODEC_ID_PRORES);
    context->pix_fmt = AV_PIX_FMT_YUV422P10LE;
    media_benchmark::ExperimentalDecoderBufferPool pool(true);
    assert(pool.attach(context.get()));

    auto frame = makeRequestedFrame();
    frame->format = AV_PIX_FMT_YUV422P10LE;
    assert(context->get_buffer2(context.get(), frame.get(), AV_GET_BUFFER_FLAG_REF) == 0);
    assertPlaneCoverageAndAlignment(frame.get());
    assert(pool.stats().prototypeFrameCount == 1);
    assert(pool.stats().fallbackCount == 0);

    pool.detach(context.get());
}

void formatChangeRetiresOldPoolsWithoutInvalidatingFrames()
{
    auto context = makeContext();
    media_benchmark::ExperimentalDecoderBufferPool pool(true);
    assert(pool.attach(context.get()));
    auto oldFrame = makeRequestedFrame();
    assert(context->get_buffer2(context.get(), oldFrame.get(), AV_GET_BUFFER_FLAG_REF) == 0);
    oldFrame->data[0][0] = 0x5a;

    auto newFrame = makeRequestedFrame(128, 96);
    assert(context->get_buffer2(context.get(), newFrame.get(), AV_GET_BUFFER_FLAG_REF) == 0);
    assert(pool.stats().poolRebuildCount == 2);
    assert(oldFrame->data[0][0] == 0x5a);
    assert(oldFrame->data[0] != newFrame->data[0]);

    pool.detach(context.get());
    pool.close();
    assert(oldFrame->data[0][0] == 0x5a);
}

void frameReferencesCanOutlivePoolFacade()
{
    auto context = makeContext();
    auto frame = makeRequestedFrame();
    {
        media_benchmark::ExperimentalDecoderBufferPool pool(true);
        assert(pool.attach(context.get()));
        assert(context->get_buffer2(context.get(), frame.get(), AV_GET_BUFFER_FLAG_REF) == 0);
        frame->data[0][0] = 0x33;
        pool.detach(context.get());
        pool.close();
    }
    context.reset();
    assert(frame->data[0][0] == 0x33);
    av_frame_unref(frame.get());
}

void disabledAndUnsupportedModesDelegateAndRestoreCallback()
{
    int marker = 7;
    {
        auto context = makeContext();
        context->get_buffer2 = fallbackBuffer;
        context->opaque = &marker;
        media_benchmark::ExperimentalDecoderBufferPool pool(false);
        assert(pool.attach(context.get()));
        auto frame = makeRequestedFrame();
        assert(context->get_buffer2(context.get(), frame.get(), 0) == 0);
        assert(pool.stats().fallbackCount == 1);
        assert(pool.stats().prototypeFrameCount == 0);
        pool.detach(context.get());
        assert(context->get_buffer2 == fallbackBuffer);
        assert(context->opaque == &marker);
    }
    {
        auto context = makeContext(AV_CODEC_ID_MPEG4);
        context->get_buffer2 = fallbackBuffer;
        media_benchmark::ExperimentalDecoderBufferPool pool(true);
        assert(pool.attach(context.get()));
        auto frame = makeRequestedFrame();
        assert(context->get_buffer2(context.get(), frame.get(), 0) == 0);
        assert(pool.stats().fallbackCount == 1);
        assert(pool.stats().prototypeFrameCount == 0);
        pool.detach(context.get());
    }
}

void hardwareContextDelegatesWithoutInspectingItsPayload()
{
    auto context = makeContext();
    context->get_buffer2 = fallbackBuffer;
    context->hw_device_ctx = av_buffer_alloc(1);
    assert(context->hw_device_ctx);
    media_benchmark::ExperimentalDecoderBufferPool pool(true);
    assert(pool.attach(context.get()));
    auto frame = makeRequestedFrame();

    assert(context->get_buffer2(context.get(), frame.get(), 0) == 0);
    assert(pool.stats().fallbackCount == 1);
    assert(pool.stats().prototypeFrameCount == 0);

    pool.detach(context.get());
    av_buffer_unref(&context->hw_device_ctx);
}

} // namespace

int main()
{
    allocatesAlignedPlanesAndReusesReleasedBuffers();
    simultaneousFramesNeverSharePlaneStorage();
    prores422TenBitUsesPrototypeBuffers();
    formatChangeRetiresOldPoolsWithoutInvalidatingFrames();
    frameReferencesCanOutlivePoolFacade();
    disabledAndUnsupportedModesDelegateAndRestoreCallback();
    hardwareContextDelegatesWithoutInspectingItsPayload();
    return 0;
}
