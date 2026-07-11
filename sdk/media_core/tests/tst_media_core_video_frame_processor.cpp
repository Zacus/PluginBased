#include "DecodePerformance.h"
#include "FFmpegUtils.h"
#include "HardwareDecoderBackend.h"
#include "VideoFrameProcessor.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

using namespace std::chrono_literals;

namespace {

media_sdk::AVFramePtr makeBufferedFrame(AVPixelFormat format,
                                        int width,
                                        int height,
                                        std::int64_t pts,
                                        AVColorRange range,
                                        AVColorSpace colorSpace)
{
    auto frame = media_sdk::makeFrame();
    frame->format = format;
    frame->width = width;
    frame->height = height;
    frame->pts = pts;
    frame->color_range = range;
    frame->colorspace = colorSpace;

    const int ret = av_frame_get_buffer(frame.get(), 32);
    assert(ret >= 0);
    assert(av_frame_make_writable(frame.get()) >= 0);
    return frame;
}

std::shared_ptr<AVFrame> storageFrame(const media_sdk::VideoFrame& frame)
{
    return std::static_pointer_cast<AVFrame>(frame.storage());
}

class FakeHardwareBackend final : public media_sdk::HardwareDecoderBackend
{
public:
    std::string_view name() const override { return "fake-hw"; }
    bool isAvailableForCodec(const AVCodec*, AVCodecID) const override { return true; }
    bool configureContext(AVCodecContext*) override { return true; }
    bool isHardwareFrame(const AVFrame* frame) const override
    {
        return frame && frame->format == AV_PIX_FMT_VIDEOTOOLBOX;
    }
    media_sdk::AVFramePtr transferToCpuFrame(const AVFrame*) override
    {
        return makeBufferedFrame(AV_PIX_FMT_NV12,
                                 8,
                                 8,
                                 77'000,
                                 AVCOL_RANGE_MPEG,
                                 AVCOL_SPC_BT709);
    }
    void reset() override {}
};

void testCpuFrameMetadataAndPlaneViews()
{
    media_sdk::VideoFrameProcessor processor;
    media_sdk::DecodePerformanceStats stats;
    auto source = makeBufferedFrame(AV_PIX_FMT_YUV420P,
                                    16,
                                    16,
                                    123'456,
                                    AVCOL_RANGE_JPEG,
                                    AVCOL_SPC_BT709);

    auto result = processor.process(std::move(source), {}, nullptr, &stats);
    assert(result.ok());

    const auto& frame = result.value();
    assert(frame.width() == 16);
    assert(frame.height() == 16);
    assert(frame.pixelFormat() == media_sdk::PixelFormat::Yuv420P);
    assert(frame.colorRange() == media_sdk::ColorRange::Full);
    assert(frame.colorSpace() == media_sdk::ColorSpace::Bt709);
    assert(frame.pts() == 123'456us);
    assert(frame.planes().size() == 3);
    assert(frame.planes()[0].data != nullptr);
    assert(frame.planes()[0].stride > 0);
    assert(frame.hasStorage());
    assert(stats.normalizedVideoFrames == 0);
    assert(stats.normalizedFrameHeaderAllocations == 0);
    assert(stats.normalizedPixelBufferAllocations == 0);
}

void testColorAndPixelFormatMapping()
{
    media_sdk::VideoFrameProcessor processor;
    auto source = makeBufferedFrame(AV_PIX_FMT_NV12,
                                    8,
                                    8,
                                    1'000,
                                    AVCOL_RANGE_MPEG,
                                    AVCOL_SPC_BT470BG);

    auto result = processor.process(std::move(source));
    assert(result.ok());
    assert(result.value().pixelFormat() == media_sdk::PixelFormat::Nv12);
    assert(result.value().colorRange() == media_sdk::ColorRange::Limited);
    assert(result.value().colorSpace() == media_sdk::ColorSpace::Bt601);
    assert(result.value().planes().size() == 2);
}

void testUnsupportedCpuFormatNormalizesToYuv420P()
{
    media_sdk::VideoFrameProcessor processor;
    media_sdk::DecodePerformanceStats stats;
    auto source = makeBufferedFrame(AV_PIX_FMT_RGB24,
                                    12,
                                    12,
                                    9'000,
                                    AVCOL_RANGE_MPEG,
                                    AVCOL_SPC_BT709);

    auto result = processor.process(std::move(source), {}, nullptr, &stats);
    assert(result.ok());
    assert(result.value().pixelFormat() == media_sdk::PixelFormat::Yuv420P);
    assert(result.value().colorRange() == media_sdk::ColorRange::Limited);
    assert(result.value().colorSpace() == media_sdk::ColorSpace::Bt709);
    assert(result.value().planes().size() == 3);
    assert(stats.normalizedVideoFrames == 1);
    assert(stats.normalizedFrameHeaderAllocations == 3);
    assert(stats.normalizedPixelBufferAllocations == 3);
    assert(stats.sourcePixelFormat == AV_PIX_FMT_RGB24);
    assert(stats.cpuPixelFormat == AV_PIX_FMT_YUV420P);
}

void testVideoToolboxNativeHandleDescriptor()
{
    media_sdk::VideoFrameProcessor processor;
    auto source = media_sdk::makeFrame();
    int pixelBuffer = 42;
    source->format = AV_PIX_FMT_VIDEOTOOLBOX;
    source->width = 1920;
    source->height = 1080;
    source->pts = 77'000;
    source->color_range = AVCOL_RANGE_MPEG;
    source->colorspace = AVCOL_SPC_BT709;
    source->data[3] = reinterpret_cast<std::uint8_t*>(&pixelBuffer);

    media_sdk::VideoFrameProcessOptions options;
    options.preferNativeVideoFrames = true;
    auto result = processor.process(std::move(source), options);
    assert(result.ok());

    const auto& frame = result.value();
    assert(frame.pixelFormat() == media_sdk::PixelFormat::Native);
    assert(frame.nativeHandle().kind == media_sdk::NativeHandleKind::VideoToolboxPixelBuffer);
    assert(frame.nativeHandle().handle == &pixelBuffer);
    assert(frame.nativeHandle().pixelFormat == AV_PIX_FMT_VIDEOTOOLBOX);
    assert(frame.planes().empty());
    assert(frame.hasStorage());
}

void testHardwareBackendUsesStringViewName()
{
    FakeHardwareBackend backend;
    assert(backend.name() == "fake-hw");
}

void testHardwareTransferDoesNotUseNormalizerAllocationPath()
{
    media_sdk::VideoFrameProcessor processor;
    media_sdk::DecodePerformanceStats stats;
    FakeHardwareBackend backend;
    auto source = media_sdk::makeFrame();
    assert(source);
    source->format = AV_PIX_FMT_VIDEOTOOLBOX;
    source->width = 8;
    source->height = 8;

    media_sdk::VideoFrameProcessOptions options;
    options.preferNativeVideoFrames = false;
    auto result = processor.process(std::move(source), options, &backend, &stats);

    assert(result.ok());
    assert(stats.hardwareVideoFrames == 1);
    assert(stats.transferredVideoFrames == 1);
    assert(stats.normalizedFrameHeaderAllocations == 0);
    assert(stats.normalizedPixelBufferAllocations == 0);
}

void testConvertedFramesReusePoolWithoutSharingInFlightSlot()
{
    media_sdk::VideoFrameProcessor processor;
    auto first = processor.process(makeBufferedFrame(AV_PIX_FMT_RGB24,
                                                     12,
                                                     12,
                                                     1'000,
                                                     AVCOL_RANGE_MPEG,
                                                     AVCOL_SPC_BT709));
    auto second = processor.process(makeBufferedFrame(AV_PIX_FMT_RGB24,
                                                      12,
                                                      12,
                                                      2'000,
                                                      AVCOL_RANGE_MPEG,
                                                      AVCOL_SPC_BT709));
    assert(first.ok());
    assert(second.ok());
    auto firstStorage = storageFrame(first.value());
    auto secondStorage = storageFrame(second.value());
    assert(firstStorage);
    assert(secondStorage);
    assert(firstStorage.get() != secondStorage.get());
    assert(first.value().planes()[0].data
           == reinterpret_cast<const std::byte*>(firstStorage->data[0]));

    AVFrame* releasedSlot = firstStorage.get();
    firstStorage.reset();
    first.value() = {};
    bool releasedSlotReused = false;
    for (int attempt = 0; attempt < 3 && !releasedSlotReused; ++attempt) {
        auto next = processor.process(makeBufferedFrame(AV_PIX_FMT_RGB24,
                                                        12,
                                                        12,
                                                        3'000 + attempt,
                                                        AVCOL_RANGE_MPEG,
                                                        AVCOL_SPC_BT709));
        assert(next.ok());
        releasedSlotReused = storageFrame(next.value()).get() == releasedSlot;
    }

    const auto stats = processor.picturePoolStats();
    assert(releasedSlotReused);
    assert(stats.allocationCount == 3);
    assert(stats.reuseCount >= 3);
    assert(stats.transientAllocationCount == 0);
}

void testResolutionChangeRetiresOldPoolEpoch()
{
    media_sdk::VideoFrameProcessor processor;
    auto oldResolution = processor.process(makeBufferedFrame(AV_PIX_FMT_RGB24,
                                                             12,
                                                             12,
                                                             1'000,
                                                             AVCOL_RANGE_MPEG,
                                                             AVCOL_SPC_BT709));
    auto newResolution = processor.process(makeBufferedFrame(AV_PIX_FMT_RGB24,
                                                             20,
                                                             16,
                                                             2'000,
                                                             AVCOL_RANGE_MPEG,
                                                             AVCOL_SPC_BT709));
    assert(oldResolution.ok());
    assert(newResolution.ok());
    assert(newResolution.value().width() == 20);
    assert(newResolution.value().height() == 16);

    oldResolution.value() = {};
    assert(processor.picturePoolStats().incompatibleReturnCount == 1);
}

void testFailedConversionDoesNotPoisonNextFrame()
{
    media_sdk::VideoFrameProcessor processor;
    auto invalid = media_sdk::makeFrame();
    assert(invalid);
    invalid->format = AV_PIX_FMT_RGB24;
    invalid->width = 0;
    invalid->height = 12;

    auto failed = processor.process(std::move(invalid));
    assert(!failed.ok());

    auto recovered = processor.process(makeBufferedFrame(AV_PIX_FMT_RGB24,
                                                         12,
                                                         12,
                                                         3'000,
                                                         AVCOL_RANGE_MPEG,
                                                         AVCOL_SPC_BT709));
    assert(recovered.ok());
    assert(recovered.value().planes().size() == 3);
}

void testNativeAndSupportedCpuFramesBypassPicturePool()
{
    media_sdk::VideoFrameProcessor processor;
    auto cpuResult = processor.process(makeBufferedFrame(AV_PIX_FMT_NV12,
                                                         8,
                                                         8,
                                                         1'000,
                                                         AVCOL_RANGE_MPEG,
                                                         AVCOL_SPC_BT709));
    assert(cpuResult.ok());
    assert(processor.picturePoolStats().acquireCount == 0);

    auto native = media_sdk::makeFrame();
    int nativeHandle = 7;
    native->format = AV_PIX_FMT_VIDEOTOOLBOX;
    native->width = 8;
    native->height = 8;
    native->data[3] = reinterpret_cast<std::uint8_t*>(&nativeHandle);
    auto nativeResult = processor.process(std::move(native));
    assert(nativeResult.ok());
    assert(nativeResult.value().pixelFormat() == media_sdk::PixelFormat::Native);
    assert(processor.picturePoolStats().acquireCount == 0);
}

void testResetKeepsOldStorageAliveAndCreatesNewPoolState()
{
    media_sdk::VideoFrameProcessor processor;
    auto beforeReset = processor.process(makeBufferedFrame(AV_PIX_FMT_RGB24,
                                                           12,
                                                           12,
                                                           1'000,
                                                           AVCOL_RANGE_MPEG,
                                                           AVCOL_SPC_BT709));
    assert(beforeReset.ok());
    auto oldStorage = storageFrame(beforeReset.value());
    const std::uint8_t* oldData = oldStorage->data[0];

    processor.reset();

    assert(oldStorage->data[0] == oldData);
    auto afterReset = processor.process(makeBufferedFrame(AV_PIX_FMT_RGB24,
                                                          12,
                                                          12,
                                                          2'000,
                                                          AVCOL_RANGE_MPEG,
                                                          AVCOL_SPC_BT709));
    assert(afterReset.ok());
    assert(processor.picturePoolStats().allocationCount == 3);
}

} // namespace

int main()
{
    testCpuFrameMetadataAndPlaneViews();
    testColorAndPixelFormatMapping();
    testUnsupportedCpuFormatNormalizesToYuv420P();
    testVideoToolboxNativeHandleDescriptor();
    testHardwareBackendUsesStringViewName();
    testHardwareTransferDoesNotUseNormalizerAllocationPath();
    testConvertedFramesReusePoolWithoutSharingInFlightSlot();
    testResolutionChangeRetiresOldPoolEpoch();
    testFailedConversionDoesNotPoisonNextFrame();
    testNativeAndSupportedCpuFramesBypassPicturePool();
    testResetKeepsOldStorageAliveAndCreatesNewPoolState();
    return 0;
}
