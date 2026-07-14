#include "DecoderBufferPool.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

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

FramePtr makeRequestedFrame(
    AVPixelFormat format = AV_PIX_FMT_YUV420P,
    int width = 64,
    int height = 48)
{
    FramePtr frame(av_frame_alloc());
    assert(frame);
    frame->format = format;
    frame->width = width;
    frame->height = height;
    return frame;
}

int customGetBuffer(AVCodecContext*, AVFrame* frame, int)
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
    media_sdk::DecoderBufferPool pool;
    assert(pool.attach(context.get()));

    auto frame = makeRequestedFrame();
    assert(context->get_buffer2(context.get(), frame.get(), AV_GET_BUFFER_FLAG_REF) == 0);
    assertPlaneCoverageAndAlignment(frame.get());
    std::array<std::uint8_t*, 3> firstData { frame->data[0], frame->data[1], frame->data[2] };
    assert(pool.stats().planeAllocationCount == 3);

    av_frame_unref(frame.get());
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = 64;
    frame->height = 48;
    assert(context->get_buffer2(context.get(), frame.get(), AV_GET_BUFFER_FLAG_REF) == 0);
    for (int plane = 0; plane < 3; ++plane)
        assert(frame->data[plane] == firstData[plane]);
    assert(pool.stats().planeAllocationCount == 3);

    pool.detach(context.get());
}

void concurrentFramesUseDistinctStorage()
{
    auto context = makeContext();
    media_sdk::DecoderBufferPool pool;
    assert(pool.attach(context.get()));
    constexpr int frameCount = 8;
    std::array<FramePtr, frameCount> frames;
    std::array<int, frameCount> results {};
    std::vector<std::thread> workers;
    workers.reserve(frameCount);

    for (int index = 0; index < frameCount; ++index) {
        frames[index] = makeRequestedFrame();
        workers.emplace_back([&, index] {
            results[index] = context->get_buffer2(
                context.get(), frames[index].get(), AV_GET_BUFFER_FLAG_REF);
        });
    }
    for (auto& worker : workers)
        worker.join();

    for (int index = 0; index < frameCount; ++index) {
        assert(results[index] == 0);
        for (int other = index + 1; other < frameCount; ++other)
            assert(frames[index]->data[0] != frames[other]->data[0]);
    }
    assert(pool.stats().pooledFrameCount == frameCount);
    pool.detach(context.get());
}

void supportsValidatedCodecAndPixelFormatSet()
{
    for (const auto codecId : { AV_CODEC_ID_H264, AV_CODEC_ID_HEVC, AV_CODEC_ID_PRORES }) {
        auto context = makeContext(codecId);
        media_sdk::DecoderBufferPool pool;
        assert(pool.attach(context.get()));
        auto frame = makeRequestedFrame(codecId == AV_CODEC_ID_PRORES
            ? AV_PIX_FMT_YUV422P10LE
            : AV_PIX_FMT_YUV420P);
        assert(context->get_buffer2(context.get(), frame.get(), AV_GET_BUFFER_FLAG_REF) == 0);
        assert(pool.stats().pooledFrameCount == 1);
        pool.detach(context.get());
    }
}

void formatChangesRetirePoolsWithoutInvalidatingFrames()
{
    auto context = makeContext();
    media_sdk::DecoderBufferPool pool;
    assert(pool.attach(context.get()));
    auto oldFrame = makeRequestedFrame();
    assert(context->get_buffer2(context.get(), oldFrame.get(), AV_GET_BUFFER_FLAG_REF) == 0);
    oldFrame->data[0][0] = 0x5a;

    auto newFrame = makeRequestedFrame(AV_PIX_FMT_YUV420P, 128, 96);
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
        media_sdk::DecoderBufferPool pool;
        assert(pool.attach(context.get()));
        assert(context->get_buffer2(context.get(), frame.get(), AV_GET_BUFFER_FLAG_REF) == 0);
        frame->data[0][0] = 0x33;
        pool.detach(context.get());
    }
    context.reset();
    assert(frame->data[0][0] == 0x33);
    av_frame_unref(frame.get());
}

void refusesContextsOwnedByOtherBufferProviders()
{
    int marker = 7;
    auto customContext = makeContext();
    customContext->get_buffer2 = customGetBuffer;
    media_sdk::DecoderBufferPool customPool;
    assert(!customPool.attach(customContext.get()));
    assert(customContext->get_buffer2 == customGetBuffer);

    auto opaqueContext = makeContext();
    opaqueContext->opaque = &marker;
    media_sdk::DecoderBufferPool opaquePool;
    assert(!opaquePool.attach(opaqueContext.get()));
    assert(opaqueContext->opaque == &marker);

    auto hardwareContext = makeContext();
    hardwareContext->hw_device_ctx = av_buffer_alloc(1);
    assert(hardwareContext->hw_device_ctx);
    media_sdk::DecoderBufferPool hardwarePool;
    assert(!hardwarePool.attach(hardwareContext.get()));
}

void unsupportedFramesFallbackAndDetachRestoresContext()
{
    auto context = makeContext();
    assert(avcodec_open2(context.get(), context->codec, nullptr) == 0);
    media_sdk::DecoderBufferPool pool;
    assert(pool.attach(context.get()));
    context->hw_device_ctx = av_buffer_alloc(1);
    assert(context->hw_device_ctx);
    auto frame = makeRequestedFrame();

    assert(context->get_buffer2(context.get(), frame.get(), 0) == 0);
    assert(pool.stats().fallbackCount == 1);
    assert(pool.stats().pooledFrameCount == 0);

    pool.detach(context.get());
    assert(context->get_buffer2 == avcodec_default_get_buffer2);
    assert(context->opaque == nullptr);
}

} // namespace

int main()
{
    allocatesAlignedPlanesAndReusesReleasedBuffers();
    concurrentFramesUseDistinctStorage();
    supportsValidatedCodecAndPixelFormatSet();
    formatChangesRetirePoolsWithoutInvalidatingFrames();
    frameReferencesCanOutlivePoolFacade();
    refusesContextsOwnedByOtherBufferProviders();
    unsupportedFramesFallbackAndDetachRestoresContext();
    return 0;
}
