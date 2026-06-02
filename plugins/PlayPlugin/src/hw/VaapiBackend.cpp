#include "VaapiBackend.h"

QString VaapiBackend::name() const
{
    return QStringLiteral("vaapi");
}

bool VaapiBackend::isAvailableForCodec(const AVCodec*, AVCodecID) const
{
    return false;
}

bool VaapiBackend::configureContext(AVCodecContext*)
{
    return false;
}

bool VaapiBackend::isHardwareFrame(const AVFrame*) const
{
    return false;
}

AVFramePtr VaapiBackend::transferToCpuFrame(const AVFrame*)
{
    return {};
}

void VaapiBackend::reset()
{
}
