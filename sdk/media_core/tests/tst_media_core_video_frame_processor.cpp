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

} // namespace

int main()
{
    testCpuFrameMetadataAndPlaneViews();
    testColorAndPixelFormatMapping();
    testUnsupportedCpuFormatNormalizesToYuv420P();
    testVideoToolboxNativeHandleDescriptor();
    testHardwareBackendUsesStringViewName();
    testHardwareTransferDoesNotUseNormalizerAllocationPath();
    return 0;
}
