#include "playback/SdkVideoFrameBridge.h"

#if defined(Q_OS_APPLE)
#include <CoreVideo/CoreVideo.h>
#endif

#include <utility>

namespace {

bool isBt709(const media_sdk::VideoFrame& frame)
{
    return frame.colorSpace() == media_sdk::ColorSpace::Bt709
        || (frame.colorSpace() == media_sdk::ColorSpace::Unknown && frame.width() >= 1280);
}

NativeVideoFrame nativeVideoFrameFrom(const media_sdk::VideoFrame& frame, const AVFrame* avFrame)
{
    NativeVideoFrame native;
    if (frame.pixelFormat() != media_sdk::PixelFormat::Native
        || frame.nativeHandle().kind != media_sdk::NativeHandleKind::VideoToolboxPixelBuffer) {
        return native;
    }

    native.kind = NativeFrameKind::VideoToolbox;
#if defined(Q_OS_APPLE)
    auto* pixelBuffer = avFrame
        ? reinterpret_cast<CVPixelBufferRef>(avFrame->data[3])
        : static_cast<CVPixelBufferRef>(frame.nativeHandle().handle);
    if (pixelBuffer) {
        const OSType cvFormat = CVPixelBufferGetPixelFormatType(pixelBuffer);
        native.pixelFormat = static_cast<int>(cvFormat);
        native.fullRange =
            cvFormat == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
            || cvFormat == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange;
        native.is10bit =
            cvFormat == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange
            || cvFormat == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange;
    }
#else
    native.pixelFormat = frame.nativeHandle().pixelFormat;
    native.fullRange = frame.colorRange() == media_sdk::ColorRange::Full;
#endif
    native.bt709 = isBt709(frame);
    if (native.pixelFormat == 0)
        native.pixelFormat = frame.nativeHandle().pixelFormat;
    return native;
}

} // namespace

VideoFrameDataPtr makeVideoFrameDataFromSdk(const media_sdk::VideoFrame& frame)
{
    const auto storage = frame.storage();
    if (!storage)
        return {};

    auto sourceFrame = std::static_pointer_cast<AVFrame>(storage);
    if (!sourceFrame)
        return {};

    const NativeVideoFrame native = nativeVideoFrameFrom(frame, sourceFrame.get());
    const bool fullRange = native.isValid()
        ? native.fullRange
        : frame.colorRange() == media_sdk::ColorRange::Full;
    const bool bt709 = native.isValid() ? native.bt709 : isBt709(frame);
    return make_video_frame(std::move(sourceFrame), fullRange, bt709, native);
}
