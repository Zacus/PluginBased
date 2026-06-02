#include "D3D11VABackend.h"

QString D3D11VABackend::name() const
{
    return QStringLiteral("d3d11va");
}

bool D3D11VABackend::isAvailableForCodec(const AVCodec*, AVCodecID) const
{
    return false;
}

bool D3D11VABackend::configureContext(AVCodecContext*)
{
    return false;
}

bool D3D11VABackend::isHardwareFrame(const AVFrame*) const
{
    return false;
}

AVFramePtr D3D11VABackend::transferToCpuFrame(const AVFrame*)
{
    return {};
}

void D3D11VABackend::reset()
{
}
