#include "VideoToolboxBackend.h"

QString VideoToolboxBackend::name() const
{
    return QStringLiteral("videotoolbox");
}

bool VideoToolboxBackend::isAvailableForCodec(const AVCodec*, AVCodecID codecId) const
{
    return codecId == AV_CODEC_ID_HEVC || codecId == AV_CODEC_ID_H264;
}

bool VideoToolboxBackend::configureContext(AVCodecContext*)
{
    return false;
}

bool VideoToolboxBackend::isHardwareFrame(const AVFrame*) const
{
    return false;
}

AVFramePtr VideoToolboxBackend::transferToCpuFrame(const AVFrame*)
{
    return {};
}

void VideoToolboxBackend::reset()
{
}
