#include "playback/SdkVideoFrameBridge.h"
#include "VideoFrameProcessor.h"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <memory>

namespace {

media_sdk::AVFramePtr bufferedRgbFrame(int width, int height, std::int64_t pts)
{
    auto frame = media_sdk::makeFrame();
    assert(frame);
    frame->format = AV_PIX_FMT_RGB24;
    frame->width = width;
    frame->height = height;
    frame->pts = pts;
    assert(av_frame_get_buffer(frame.get(), 32) >= 0);
    assert(av_frame_make_writable(frame.get()) >= 0);
    return frame;
}

std::shared_ptr<AVFrame> trackedFrame(std::atomic_int& releaseCount)
{
    AVFrame* rawFrame = av_frame_alloc();
    assert(rawFrame);
    return std::shared_ptr<AVFrame>(rawFrame, [&releaseCount](AVFrame* frame) {
        releaseCount.fetch_add(1, std::memory_order_relaxed);
        av_frame_free(&frame);
    });
}

void cpuFrameDataSharesSdkStorageLifetime()
{
    std::atomic_int releaseCount { 0 };
    auto storage = trackedFrame(releaseCount);
    storage->format = AV_PIX_FMT_YUV420P;
    storage->width = 16;
    storage->height = 16;
    AVFrame* rawFrame = storage.get();
    media_sdk::VideoFrame sdkFrame({
        .width = 16,
        .height = 16,
        .pixelFormat = media_sdk::PixelFormat::Yuv420P,
        .colorRange = media_sdk::ColorRange::Limited,
        .colorSpace = media_sdk::ColorSpace::Bt709,
        .storage = storage,
    });

    auto frameData = makeVideoFrameDataFromSdk(sdkFrame);
    assert(frameData);
    assert(frameData->frame.get() == rawFrame);
    assert(!frameData->fullRange);
    assert(frameData->bt709);

    sdkFrame = {};
    storage.reset();
    assert(releaseCount.load(std::memory_order_relaxed) == 0);

    frameData.reset();
    assert(releaseCount.load(std::memory_order_relaxed) == 1);
}

void nativeFrameDataSharesSdkStorageLifetime()
{
    std::atomic_int releaseCount { 0 };
    auto storage = trackedFrame(releaseCount);
    storage->format = AV_PIX_FMT_VIDEOTOOLBOX;
    storage->width = 1920;
    storage->height = 1080;
    AVFrame* rawFrame = storage.get();
    media_sdk::VideoFrame sdkFrame({
        .width = 1920,
        .height = 1080,
        .pixelFormat = media_sdk::PixelFormat::Native,
        .colorRange = media_sdk::ColorRange::Limited,
        .colorSpace = media_sdk::ColorSpace::Bt709,
        .nativeHandle = {
            .kind = media_sdk::NativeHandleKind::VideoToolboxPixelBuffer,
            .handle = nullptr,
            .pixelFormat = 42,
        },
        .storage = storage,
    });

    auto frameData = makeVideoFrameDataFromSdk(sdkFrame);
    assert(frameData);
    assert(frameData->frame.get() == rawFrame);
    assert(frameData->native.kind == NativeFrameKind::VideoToolbox);
    assert(frameData->native.pixelFormat == 42);

    sdkFrame = {};
    storage.reset();
    assert(releaseCount.load(std::memory_order_relaxed) == 0);

    frameData.reset();
    assert(releaseCount.load(std::memory_order_relaxed) == 1);
}

void missingStorageIsRejected()
{
    assert(!makeVideoFrameDataFromSdk(media_sdk::VideoFrame {}));
}

void pooledRenderReferenceOutlivesProcessorFacade()
{
    VideoFrameDataPtr renderFrame;
    {
        media_sdk::VideoFrameProcessor processor;
        auto converted = processor.process(bufferedRgbFrame(16, 16, 1'000));
        assert(converted.ok());
        renderFrame = makeVideoFrameDataFromSdk(converted.value());
        assert(renderFrame);
        assert(processor.picturePoolStats().inFlightCount == 1);
    }

    assert(renderFrame->frame);
    assert(renderFrame->frame->data[0] != nullptr);
    renderFrame.reset();
}

void repeatedProcessorCloseWithRenderFramesIsStable()
{
    for (int iteration = 0; iteration < 20; ++iteration) {
        VideoFrameDataPtr renderFrame;
        {
            media_sdk::VideoFrameProcessor processor;
            auto converted = processor.process(bufferedRgbFrame(16, 16, iteration));
            assert(converted.ok());
            renderFrame = makeVideoFrameDataFromSdk(converted.value());
        }
        assert(renderFrame && renderFrame->frame->data[0]);
        renderFrame.reset();
    }
}

} // namespace

int main()
{
    cpuFrameDataSharesSdkStorageLifetime();
    nativeFrameDataSharesSdkStorageLifetime();
    missingStorageIsRejected();
    pooledRenderReferenceOutlivesProcessorFacade();
    repeatedProcessorCloseWithRenderFramesIsStable();
    return 0;
}
