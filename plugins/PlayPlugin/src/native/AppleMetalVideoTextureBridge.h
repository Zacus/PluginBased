#pragma once

#include "NativeVideoFrame.h"

#include <QSize>

#include <memory>

class QRhi;
class QRhiTexture;
struct AVFrame;

struct AppleMetalTextureSet
{
    ~AppleMetalTextureSet();

    std::unique_ptr<QRhiTexture> yTexture;
    std::unique_ptr<QRhiTexture> uvTexture;
    std::unique_ptr<QRhiTexture> vPlaceholderTexture;
    void* yCoreVideoTexture = nullptr;
    void* uvCoreVideoTexture = nullptr;
    QSize lumaSize;
    QSize chromaSize;
    NativeVideoFrame native;
};

class AppleMetalVideoTextureBridge
{
public:
    AppleMetalVideoTextureBridge();
    ~AppleMetalVideoTextureBridge();

    AppleMetalVideoTextureBridge(const AppleMetalVideoTextureBridge&) = delete;
    AppleMetalVideoTextureBridge& operator=(const AppleMetalVideoTextureBridge&) = delete;

    bool isAvailable(QRhi* rhi) const;
    std::unique_ptr<AppleMetalTextureSet> createTextureSet(QRhi* rhi, const AVFrame* frame);

private:
    void* m_textureCache = nullptr;
    void* m_device = nullptr;
};
