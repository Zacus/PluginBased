#include "VideoToolboxBackend.h"
#include "Logger.h"

namespace {

bool decoderSupportsVideoToolbox(const AVCodec* codec)
{
    if (!codec)
        return false;

    for (int i = 0;; ++i)
    {
        const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
        if (!config)
            return false;
        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
            config->device_type == AV_HWDEVICE_TYPE_VIDEOTOOLBOX &&
            config->pix_fmt == AV_PIX_FMT_VIDEOTOOLBOX)
            return true;
    }
}

AVPixelFormat selectVideoToolboxFormat(AVCodecContext*, const AVPixelFormat* formats)
{
    for (const AVPixelFormat* format = formats; *format != AV_PIX_FMT_NONE; ++format)
    {
        if (*format == AV_PIX_FMT_VIDEOTOOLBOX)
            return *format;
    }
    return formats[0];
}

} // namespace

QString VideoToolboxBackend::name() const
{
    return QStringLiteral("videotoolbox");
}

bool VideoToolboxBackend::isAvailableForCodec(const AVCodec* codec, AVCodecID codecId) const
{
    if (codecId != AV_CODEC_ID_HEVC && codecId != AV_CODEC_ID_H264)
        return false;
    return decoderSupportsVideoToolbox(codec);
}

bool VideoToolboxBackend::configureContext(AVCodecContext* codecContext)
{
    reset();

    AVBufferRef* rawDevice = nullptr;
    int ret = av_hwdevice_ctx_create(&rawDevice,
                                     AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
                                     nullptr,
                                     nullptr,
                                     0);
    if (ret < 0)
    {
        LOG_WARN("VideoToolboxBackend: av_hwdevice_ctx_create failed: {}", av_err(ret));
        return false;
    }

    m_deviceContext.reset(rawDevice);
    codecContext->get_format = selectVideoToolboxFormat;
    codecContext->hw_device_ctx = av_buffer_ref(m_deviceContext.get());
    if (!codecContext->hw_device_ctx)
    {
        LOG_WARN("VideoToolboxBackend: av_buffer_ref failed");
        reset();
        return false;
    }

    LOG_INFO("VideoToolboxBackend: configured");
    return true;
}

bool VideoToolboxBackend::isHardwareFrame(const AVFrame* frame) const
{
    return frame && frame->format == AV_PIX_FMT_VIDEOTOOLBOX;
}

AVFramePtr VideoToolboxBackend::transferToCpuFrame(const AVFrame* frame)
{
    if (!isHardwareFrame(frame))
        return {};

    auto cpuFrame = make_frame();
    const int ret = av_hwframe_transfer_data(cpuFrame.get(), frame, 0);
    if (ret < 0)
        return {};

    return cpuFrame;
}

void VideoToolboxBackend::reset()
{
    m_deviceContext.reset();
}
