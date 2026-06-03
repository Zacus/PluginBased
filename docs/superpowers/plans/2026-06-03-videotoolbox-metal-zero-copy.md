# VideoToolbox Metal Zero-Copy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a macOS Metal zero-copy render path for VideoToolbox `AV_PIX_FMT_VIDEOTOOLBOX` frames while keeping the existing CPU NV12/P010 path as fallback.

**Architecture:** Keep decoder, renderer, and surface responsibilities separated. Decoder decides whether to preserve VideoToolbox hardware frames; `FFmpegSurface` binds either CPU-uploaded QRhi textures or Apple native textures; `AppleMetalVideoTextureBridge.mm` owns the CoreVideo/Metal/QRhi bridge.

**Tech Stack:** C++17, Objective-C++, Qt 6 QRhi/Scene Graph, FFmpeg VideoToolbox frames, CoreVideo `CVPixelBufferRef`, `CVMetalTextureCache`, Metal textures.

---

## File Structure

- Modify `tests/playplugin_regression_checks.py`: static checks for native bridge, direct hardware frame preservation, fallback, and P010 high-bit handling.
- Modify `plugins/PlayPlugin/src/FFmpegUtils.h`: add native frame metadata fields to `VideoFrameData`.
- Modify `plugins/PlayPlugin/src/FFmpegDecoder.h`: add direct-render counters and helpers.
- Modify `plugins/PlayPlugin/src/FFmpegDecoder.cpp`: preserve VideoToolbox frames when direct render is enabled and keep CPU fallback.
- Modify `plugins/PlayPlugin/src/PlayerEngine.h`: route native-render capability and failure feedback between Surface and Decoder.
- Modify `plugins/PlayPlugin/src/PlayerEngine.cpp`: enable VideoToolbox direct render only when Surface confirms Metal RHI support.
- Modify `plugins/PlayPlugin/src/FFmpegSurface.cpp`: consume native frames and bind native QRhi textures.
- Create `plugins/PlayPlugin/src/native/NativeVideoFrame.h`: small cross-file value types for native rendering.
- Create `plugins/PlayPlugin/src/native/AppleMetalVideoTextureBridge.h`: C++ interface for Apple bridge.
- Create `plugins/PlayPlugin/src/native/AppleMetalVideoTextureBridge.mm`: Objective-C++ implementation using CoreVideo and Metal.
- Modify `plugins/PlayPlugin/CMakeLists.txt`: compile `.mm` on Apple and link Apple frameworks.

### Task 1: Regression Checks for Native Zero-Copy

**Files:**
- Modify: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: Write failing static checks**

Add these reads near the other file reads:

```python
native_frame_h = read("plugins/PlayPlugin/src/native/NativeVideoFrame.h")
apple_bridge_h = read("plugins/PlayPlugin/src/native/AppleMetalVideoTextureBridge.h")
apple_bridge_mm = read("plugins/PlayPlugin/src/native/AppleMetalVideoTextureBridge.mm")
```

Add these requirements after the existing NV12/P010 checks:

```python
require("NativeVideoFrame" in native_frame_h and "NativeFrameKind" in native_frame_h,
        "native video frame metadata should exist for hardware-backed frames")
require("CVMetalTextureCacheCreateTextureFromImage" in apple_bridge_mm,
        "Apple bridge should create Metal textures from CVPixelBuffer planes")
require("QRhiTexture::NativeTexture" in apple_bridge_mm and "createFrom" in apple_bridge_mm,
        "Apple bridge should wrap Metal textures with QRhiTexture::createFrom")
require("AV_PIX_FMT_VIDEOTOOLBOX" in decoder_cpp and "shouldPreserveHardwareFrameForDirectRender" in decoder_cpp,
        "decoder should preserve VideoToolbox frames when native render is enabled")
require("transferHardwareFrameToCpu" in decoder_cpp and "nativeFallbackVideoFrames" in decoder_h,
        "native render failures should be observable and keep CPU fallback available")
require("AppleMetalVideoTextureBridge" in surface_cpp and "setNativeFrame" in surface_cpp,
        "FFmpegSurface should consume native VideoToolbox frames")
require("supportsNativeVideoToolboxRendering" in surface_cpp and
        "setVideoToolboxDirectRenderingEnabled" in decoder_h and
        "nativeRenderingFailed" in surface_cpp,
        "native rendering should be enabled by a Surface-to-Decoder capability handshake")
require("CoreVideo" in cmake and "Metal" in cmake and "QuartzCore" in cmake,
        "PlayPlugin should link Apple frameworks for CVMetalTextureCache")
```

- [ ] **Step 2: Run the regression script and confirm RED**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: FAIL because `plugins/PlayPlugin/src/native/NativeVideoFrame.h` does not exist yet.

- [ ] **Step 3: Commit the failing test**

Run:

```bash
git add tests/playplugin_regression_checks.py
git commit -m "[测试] 增加VideoToolbox零拷贝回归检查"
```

Expected: commit succeeds.

### Task 2: Native Frame Metadata

**Files:**
- Create: `plugins/PlayPlugin/src/native/NativeVideoFrame.h`
- Modify: `plugins/PlayPlugin/src/FFmpegUtils.h`
- Test: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: Create native metadata types**

Create `plugins/PlayPlugin/src/native/NativeVideoFrame.h`:

```cpp
#pragma once

#include <QtGlobal>

enum class NativeFrameKind
{
    None,
    VideoToolbox
};

struct NativeVideoFrame
{
    NativeFrameKind kind = NativeFrameKind::None;
    int pixelFormat = 0;
    bool fullRange = false;
    bool bt709 = false;
    bool is10bit = false;

    bool isValid() const
    {
        return kind != NativeFrameKind::None && pixelFormat != 0;
    }
};
```

- [ ] **Step 2: Add metadata to VideoFrameData**

In `plugins/PlayPlugin/src/FFmpegUtils.h`, include the new header and add a field:

```cpp
#include "native/NativeVideoFrame.h"
```

Inside `struct VideoFrameData` add:

```cpp
NativeVideoFrame native;
```

Update `make_video_frame()` to preserve the default native metadata:

```cpp
inline VideoFrameDataPtr make_video_frame(AVFramePtr frame,
                                          bool fullRange,
                                          bool bt709,
                                          NativeVideoFrame native = {})
{
    auto data = std::make_shared<VideoFrameData>();
    data->frame = std::move(frame);
    data->fullRange = fullRange;
    data->bt709 = bt709;
    data->native = native;
    return data;
}
```

- [ ] **Step 3: Run regression script**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: still FAIL because Apple bridge and decoder direct path are not implemented yet.

- [ ] **Step 4: Commit metadata types**

Run:

```bash
git add plugins/PlayPlugin/src/native/NativeVideoFrame.h plugins/PlayPlugin/src/FFmpegUtils.h
git commit -m "[功能新增] 增加原生视频帧元数据"
```

Expected: commit succeeds.

### Task 3: Apple Bridge Build Integration

**Files:**
- Create: `plugins/PlayPlugin/src/native/AppleMetalVideoTextureBridge.h`
- Create: `plugins/PlayPlugin/src/native/AppleMetalVideoTextureBridge.mm`
- Modify: `plugins/PlayPlugin/CMakeLists.txt`
- Test: `cmake --build build --parallel`

- [ ] **Step 1: Add bridge header**

Create `plugins/PlayPlugin/src/native/AppleMetalVideoTextureBridge.h`:

```cpp
#pragma once

#include "NativeVideoFrame.h"

#include <QSize>

#include <memory>

class QRhi;
class QRhiTexture;
struct AVFrame;

struct AppleMetalTextureSet
{
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
```

- [ ] **Step 2: Add bridge Objective-C++ implementation skeleton**

Create `plugins/PlayPlugin/src/native/AppleMetalVideoTextureBridge.mm`:

```objective-c++
#include "AppleMetalVideoTextureBridge.h"

#include <QDebug>

#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

AppleMetalVideoTextureBridge::AppleMetalVideoTextureBridge() = default;

AppleMetalVideoTextureBridge::~AppleMetalVideoTextureBridge()
{
    if (m_textureCache)
        CFRelease(static_cast<CVMetalTextureCacheRef>(m_textureCache));
}

bool AppleMetalVideoTextureBridge::isAvailable(QRhi* rhi) const
{
    if (!rhi || rhi->backend() != QRhi::Metal)
        return false;
    return true;
}

std::unique_ptr<AppleMetalTextureSet>
AppleMetalVideoTextureBridge::createTextureSet(QRhi*, const AVFrame*)
{
    return {};
}
```

- [ ] **Step 3: Add CMake entries**

In `plugins/PlayPlugin/CMakeLists.txt`, add these files to `SOURCES`:

```cmake
src/native/NativeVideoFrame.h
src/native/AppleMetalVideoTextureBridge.h
src/native/AppleMetalVideoTextureBridge.mm
```

Add Apple framework linking:

```cmake
if(APPLE)
    target_link_libraries(PlayPlugin PRIVATE
        "-framework CoreVideo"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework VideoToolbox"
    )
endif()
```

- [ ] **Step 4: Run build**

Run:

```bash
cmake --build build --parallel
```

Expected: build succeeds and compiles `AppleMetalVideoTextureBridge.mm`.

- [ ] **Step 5: Commit build integration**

Run:

```bash
git add plugins/PlayPlugin/CMakeLists.txt plugins/PlayPlugin/src/native/AppleMetalVideoTextureBridge.h plugins/PlayPlugin/src/native/AppleMetalVideoTextureBridge.mm
git commit -m "[功能新增] 接入Apple Metal纹理桥接骨架"
```

Expected: commit succeeds.

### Task 4: Decoder Direct-Frame Preservation

**Files:**
- Modify: `plugins/PlayPlugin/src/FFmpegDecoder.h`
- Modify: `plugins/PlayPlugin/src/FFmpegDecoder.cpp`
- Test: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: Add control API, counters, and helper declarations**

In `FFmpegDecoder.h`, add counters to `DecodePerformanceStats`:

```cpp
qint64 nativeVideoFrames = 0;
qint64 nativeFallbackVideoFrames = 0;
```

Add a public slot:

```cpp
void setVideoToolboxDirectRenderingEnabled(bool enabled);
```

Add private helper declarations:

```cpp
bool shouldPreserveHardwareFrameForDirectRender(const AVFrame* frame) const;
NativeVideoFrame makeNativeVideoFrameMetadata(const AVFrame* frame) const;
```

Add the runtime flag:

```cpp
bool m_videoToolboxDirectRenderingEnabled = false;
```

- [ ] **Step 2: Implement direct preservation helper**

In `FFmpegDecoder.cpp`, include CoreVideo only on Apple:

```cpp
#if defined(Q_OS_APPLE)
#include <CoreVideo/CoreVideo.h>
#endif
```

Add:

```cpp
bool FFmpegDecoder::shouldPreserveHardwareFrameForDirectRender(const AVFrame* frame) const
{
#if defined(Q_OS_APPLE)
    return m_videoToolboxDirectRenderingEnabled &&
           m_hardwareDecoder &&
           frame &&
           frame->format == AV_PIX_FMT_VIDEOTOOLBOX;
#else
    Q_UNUSED(frame);
    return false;
#endif
}
```

Add the slot:

```cpp
void FFmpegDecoder::setVideoToolboxDirectRenderingEnabled(bool enabled)
{
    m_videoToolboxDirectRenderingEnabled = enabled;
}
```

Add:

```cpp
NativeVideoFrame FFmpegDecoder::makeNativeVideoFrameMetadata(const AVFrame* frame) const
{
    NativeVideoFrame native;
#if defined(Q_OS_APPLE)
    if (!frame || frame->format != AV_PIX_FMT_VIDEOTOOLBOX)
        return native;

    auto* pixelBuffer = reinterpret_cast<CVPixelBufferRef>(frame->data[3]);
    if (!pixelBuffer)
        return native;

    const OSType cvFormat = CVPixelBufferGetPixelFormatType(pixelBuffer);
    native.kind = NativeFrameKind::VideoToolbox;
    native.pixelFormat = static_cast<int>(cvFormat);
    native.fullRange =
        cvFormat == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange ||
        cvFormat == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange;
    native.is10bit =
        cvFormat == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange ||
        cvFormat == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange;
    native.bt709 =
        frame->colorspace == AVCOL_SPC_BT709 ||
        (frame->colorspace == AVCOL_SPC_UNSPECIFIED && frame->width >= 1280);
#else
    Q_UNUSED(frame);
#endif
    return native;
}
```

- [ ] **Step 3: Preserve hardware frames in prepareVideoFrameForQueue**

At the start of `prepareVideoFrameForQueue()` add:

```cpp
if (shouldPreserveHardwareFrameForDirectRender(frame.get()))
{
    ++m_decodePerf.nativeVideoFrames;
    return frame;
}
```

- [ ] **Step 4: Include native counters in performance log**

Extend the decoder log format with `native={}` and `native_fallback={}` and pass:

```cpp
m_decodePerf.nativeVideoFrames,
m_decodePerf.nativeFallbackVideoFrames,
```

- [ ] **Step 5: Run regression script**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: still FAIL because bridge implementation and surface native consumption are incomplete.

- [ ] **Step 6: Commit decoder direct preservation**

Run:

```bash
git add plugins/PlayPlugin/src/FFmpegDecoder.h plugins/PlayPlugin/src/FFmpegDecoder.cpp
git commit -m "[功能新增] 保留VideoToolbox原生帧"
```

Expected: commit succeeds.

### Task 5: Apple Bridge Texture Creation

**Files:**
- Modify: `plugins/PlayPlugin/src/native/AppleMetalVideoTextureBridge.h`
- Modify: `plugins/PlayPlugin/src/native/AppleMetalVideoTextureBridge.mm`
- Test: `cmake --build build --parallel`

- [ ] **Step 1: Add texture set cleanup**

In `AppleMetalVideoTextureBridge.mm`, add:

```objective-c++
AppleMetalTextureSet::~AppleMetalTextureSet()
{
    if (yCoreVideoTexture)
        CFRelease(static_cast<CVMetalTextureRef>(yCoreVideoTexture));
    if (uvCoreVideoTexture)
        CFRelease(static_cast<CVMetalTextureRef>(uvCoreVideoTexture));
}
```

Add destructor declaration in `AppleMetalVideoTextureBridge.h`:

```cpp
~AppleMetalTextureSet();
```

- [ ] **Step 2: Add format mapping helper**

In `AppleMetalVideoTextureBridge.mm`, add an unnamed namespace:

```objective-c++
namespace {

struct MetalPlaneFormat
{
    MTLPixelFormat yFormat = MTLPixelFormatInvalid;
    MTLPixelFormat uvFormat = MTLPixelFormatInvalid;
    QRhiTexture::Format yRhiFormat = QRhiTexture::UnknownFormat;
    QRhiTexture::Format uvRhiFormat = QRhiTexture::UnknownFormat;
    bool fullRange = false;
    bool is10bit = false;
    float formatMode = 2.0f;
};

MetalPlaneFormat mapPixelBufferFormat(OSType format)
{
    switch (format) {
    case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
        return { MTLPixelFormatR8Unorm, MTLPixelFormatRG8Unorm,
                 QRhiTexture::R8, QRhiTexture::RG8, false, false, 2.0f };
    case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
        return { MTLPixelFormatR8Unorm, MTLPixelFormatRG8Unorm,
                 QRhiTexture::R8, QRhiTexture::RG8, true, false, 2.0f };
    case kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange:
        return { MTLPixelFormatR16Unorm, MTLPixelFormatRG16Unorm,
                 QRhiTexture::R16, QRhiTexture::RG16, false, true, 3.0f };
    case kCVPixelFormatType_420YpCbCr10BiPlanarFullRange:
        return { MTLPixelFormatR16Unorm, MTLPixelFormatRG16Unorm,
                 QRhiTexture::R16, QRhiTexture::RG16, true, true, 3.0f };
    default:
        return {};
    }
}

} // namespace
```

- [ ] **Step 3: Implement texture cache creation**

Inside `createTextureSet()`, obtain Metal native handles and create cache:

```objective-c++
auto* handles = static_cast<const QRhiMetalNativeHandles*>(rhi->nativeHandles());
if (!handles || !handles->dev)
    return {};

if (!m_textureCache || m_device != handles->dev) {
    if (m_textureCache)
        CFRelease(static_cast<CVMetalTextureCacheRef>(m_textureCache));
    m_textureCache = nullptr;
    m_device = handles->dev;
    CVMetalTextureCacheRef cache = nullptr;
    const CVReturn ret = CVMetalTextureCacheCreate(kCFAllocatorDefault,
                                                   nullptr,
                                                   handles->dev,
                                                   nullptr,
                                                   &cache);
    if (ret != kCVReturnSuccess)
        return {};
    m_textureCache = cache;
}
```

- [ ] **Step 4: Create CVMetalTextureRef for both planes**

Continue inside `createTextureSet()`:

```objective-c++
if (!frame || frame->format != AV_PIX_FMT_VIDEOTOOLBOX)
    return {};

auto* pixelBuffer = reinterpret_cast<CVPixelBufferRef>(frame->data[3]);
if (!pixelBuffer || CVPixelBufferGetPlaneCount(pixelBuffer) < 2)
    return {};

const OSType cvFormat = CVPixelBufferGetPixelFormatType(pixelBuffer);
const MetalPlaneFormat fmt = mapPixelBufferFormat(cvFormat);
if (fmt.yFormat == MTLPixelFormatInvalid)
    return {};

const size_t yWidth = CVPixelBufferGetWidthOfPlane(pixelBuffer, 0);
const size_t yHeight = CVPixelBufferGetHeightOfPlane(pixelBuffer, 0);
const size_t uvWidth = CVPixelBufferGetWidthOfPlane(pixelBuffer, 1);
const size_t uvHeight = CVPixelBufferGetHeightOfPlane(pixelBuffer, 1);

CVMetalTextureRef yCvTexture = nullptr;
CVMetalTextureRef uvCvTexture = nullptr;

CVReturn ret = CVMetalTextureCacheCreateTextureFromImage(
    kCFAllocatorDefault,
    static_cast<CVMetalTextureCacheRef>(m_textureCache),
    pixelBuffer,
    nullptr,
    fmt.yFormat,
    yWidth,
    yHeight,
    0,
    &yCvTexture);
if (ret != kCVReturnSuccess || !yCvTexture)
    return {};

ret = CVMetalTextureCacheCreateTextureFromImage(
    kCFAllocatorDefault,
    static_cast<CVMetalTextureCacheRef>(m_textureCache),
    pixelBuffer,
    nullptr,
    fmt.uvFormat,
    uvWidth,
    uvHeight,
    1,
    &uvCvTexture);
if (ret != kCVReturnSuccess || !uvCvTexture) {
    CFRelease(yCvTexture);
    return {};
}
```

- [ ] **Step 5: Wrap Metal textures in QRhiTexture**

Continue inside `createTextureSet()`:

```objective-c++
id<MTLTexture> yMetalTexture = CVMetalTextureGetTexture(yCvTexture);
id<MTLTexture> uvMetalTexture = CVMetalTextureGetTexture(uvCvTexture);
if (!yMetalTexture || !uvMetalTexture) {
    CFRelease(yCvTexture);
    CFRelease(uvCvTexture);
    return {};
}

auto set = std::make_unique<AppleMetalTextureSet>();
set->lumaSize = QSize(static_cast<int>(yWidth), static_cast<int>(yHeight));
set->chromaSize = QSize(static_cast<int>(uvWidth), static_cast<int>(uvHeight));
set->native.kind = NativeFrameKind::VideoToolbox;
set->native.pixelFormat = static_cast<int>(cvFormat);
set->native.fullRange = fmt.fullRange;
set->native.is10bit = fmt.is10bit;
set->native.bt709 =
    frame->colorspace == AVCOL_SPC_BT709 ||
    (frame->colorspace == AVCOL_SPC_UNSPECIFIED && frame->width >= 1280);

set->yTexture.reset(rhi->newTexture(fmt.yRhiFormat, set->lumaSize));
set->uvTexture.reset(rhi->newTexture(fmt.uvRhiFormat, set->chromaSize));
set->vPlaceholderTexture.reset(rhi->newTexture(QRhiTexture::R8, QSize(1, 1)));

QRhiTexture::NativeTexture yNative = { reinterpret_cast<quint64>(yMetalTexture), 0 };
QRhiTexture::NativeTexture uvNative = { reinterpret_cast<quint64>(uvMetalTexture), 0 };
if (!set->yTexture->createFrom(yNative) || !set->uvTexture->createFrom(uvNative) ||
    !set->vPlaceholderTexture->create()) {
    return {};
}

set->yCoreVideoTexture = yCvTexture;
set->uvCoreVideoTexture = uvCvTexture;
return set;
```

- [ ] **Step 6: Run build**

Run:

```bash
cmake --build build --parallel
```

Expected: build succeeds.

- [ ] **Step 7: Commit bridge implementation**

Run:

```bash
git add plugins/PlayPlugin/src/native/AppleMetalVideoTextureBridge.h plugins/PlayPlugin/src/native/AppleMetalVideoTextureBridge.mm
git commit -m "[功能新增] 创建VideoToolbox Metal原生纹理"
```

Expected: commit succeeds.

### Task 6: Surface Native Frame Consumption

**Files:**
- Modify: `plugins/PlayPlugin/src/FFmpegSurface.h`
- Modify: `plugins/PlayPlugin/src/FFmpegSurface.cpp`
- Test: `tests/playplugin_regression_checks.py`
- Test: `cmake --build build --parallel`

- [ ] **Step 1: Include Apple bridge conditionally**

At the top of `FFmpegSurface.cpp`, add:

```cpp
#if defined(Q_OS_APPLE)
#include "native/AppleMetalVideoTextureBridge.h"
#endif
```

- [ ] **Step 2: Add Surface capability API and failure signal**

In `FFmpegSurface.h`, add:

```cpp
Q_INVOKABLE bool supportsNativeVideoToolboxRendering() const;

signals:
    void nativeRenderingFailed();
```

In `FFmpegSurface.cpp`, implement:

```cpp
bool FFmpegSurface::supportsNativeVideoToolboxRendering() const
{
#if defined(Q_OS_APPLE)
    if (!window() || !window()->rendererInterface())
        return false;
    return window()->rendererInterface()->graphicsApi() == QSGRendererInterface::MetalRhi;
#else
    return false;
#endif
}
```

- [ ] **Step 3: Extend VideoMaterial**

Inside `class VideoMaterial`, add:

```cpp
#if defined(Q_OS_APPLE)
std::unique_ptr<AppleMetalTextureSet> nativeTextures;
#endif
bool usingNativeTextures = false;
```

- [ ] **Step 4: Add native frame setter to VideoNode**

Add this method to `VideoNode`:

```cpp
bool setNativeFrame(QQuickWindow* window, const VideoFrameDataPtr& frameData)
{
#if defined(Q_OS_APPLE)
    if (!window || !frameData || !frameData->frame ||
        frameData->native.kind != NativeFrameKind::VideoToolbox)
        return false;

    if (!m_nativeBridge)
        m_nativeBridge = std::make_unique<AppleMetalVideoTextureBridge>();
    if (!m_nativeBridge->isAvailable(window->rhi()))
        return false;

    auto textures = m_nativeBridge->createTextureSet(window->rhi(), frameData->frame.get());
    if (!textures)
        return false;

    releaseTextures();
    m_material_.tex_y = textures->yTexture.get();
    m_material_.tex_u = textures->uvTexture.get();
    m_material_.tex_v = textures->vPlaceholderTexture.get();
    m_material_.size = textures->lumaSize;
    m_material_.fmtInfo = PixelFormatInfo::fromAVFormat(textures->native.is10bit
        ? AV_PIX_FMT_P010LE
        : AV_PIX_FMT_NV12);
    m_material_.fullRange = textures->native.fullRange;
    m_material_.bt709 = textures->native.bt709;
    m_material_.cachedFullRange = textures->native.fullRange;
    m_material_.cachedBt709 = textures->native.bt709;
    m_material_.paramsDirty = true;
    m_material_.pending.valid = false;
    m_material_.pending.frameData.reset();
    m_material_.currentFrame = frameData;
    m_material_.nativeTextures = std::move(textures);
    m_material_.usingNativeTextures = true;
    m_hasFrame = true;
    markDirty(QSGNode::DirtyMaterial);
    return true;
#else
    Q_UNUSED(window);
    Q_UNUSED(frameData);
    return false;
#endif
}
```

Add a private bridge member:

```cpp
#if defined(Q_OS_APPLE)
std::unique_ptr<AppleMetalVideoTextureBridge> m_nativeBridge;
#endif
```

- [ ] **Step 5: Protect native QRhiTexture ownership**

Update `releaseTextures()` so native wrapped textures are released by resetting `nativeTextures`, not by deleting borrowed raw pointers twice:

```cpp
if (m_material_.usingNativeTextures) {
#if defined(Q_OS_APPLE)
    m_material_.nativeTextures.reset();
#endif
    m_material_.tex_y = nullptr;
    m_material_.tex_u = nullptr;
    m_material_.tex_v = nullptr;
    m_material_.usingNativeTextures = false;
} else {
    delete m_material_.tex_y;
    delete m_material_.tex_u;
    delete m_material_.tex_v;
}
m_material_.tex_y = nullptr;
m_material_.tex_u = nullptr;
m_material_.tex_v = nullptr;
m_material_.size = {};
m_material_.currentFrame.reset();
```

- [ ] **Step 6: Route native frames before CPU upload**

Change `VideoNode::setFrame()` from `void` to `bool`. At the current early returns, return `false` when `window`, `frameData`, or `frame` is invalid. For unsupported CPU formats, return `false`.

At the start of `VideoNode::setFrame()`, before `PixelFormatInfo::fromAVFormat(frame->format)`, add:

```cpp
if (frameData->native.kind == NativeFrameKind::VideoToolbox)
    return setNativeFrame(window, frameData);
```

At the end of successful CPU frame setup, return `true`.

In `FFmpegSurface::updatePaintNode()`, replace:

```cpp
if (frame)
    node->setFrame(window(), frame);
```

with:

```cpp
if (frame && !node->setFrame(window(), frame) &&
    frame->native.kind == NativeFrameKind::VideoToolbox) {
    emit nativeRenderingFailed();
}
```

The current native frame is dropped because CPU transfer is not available in the render thread; Task 7 disables direct rendering for later frames.

- [ ] **Step 7: Run regression and build**

Run:

```bash
python3 tests/playplugin_regression_checks.py
cmake --build build --parallel
```

Expected: regression still may FAIL until fallback logic is complete; build must succeed.

- [ ] **Step 8: Commit surface native consumption**

Run:

```bash
git add plugins/PlayPlugin/src/FFmpegSurface.h plugins/PlayPlugin/src/FFmpegSurface.cpp
git commit -m "[功能新增] Surface绑定VideoToolbox原生纹理"
```

Expected: commit succeeds.

### Task 7: Fallback Completion and Observability

**Files:**
- Modify: `plugins/PlayPlugin/src/PlayerEngine.h`
- Modify: `plugins/PlayPlugin/src/PlayerEngine.cpp`
- Modify: `plugins/PlayPlugin/src/FFmpegDecoder.h`
- Modify: `plugins/PlayPlugin/src/FFmpegDecoder.cpp`
- Modify: `plugins/PlayPlugin/src/FFmpegSurface.cpp`
- Test: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: Add PlayerEngine native-render control**

In `PlayerEngine.h`, add:

```cpp
void updateNativeVideoRenderingEnabled();
void onNativeRenderingFailed();
bool m_nativeVideoRenderingEnabled = false;
```

In `PlayerEngine::setSurface()`, after connecting `frameReady`, connect the failure signal and update capability:

```cpp
if (m_surface) {
    connect(m_surface.data(), &FFmpegSurface::nativeRenderingFailed,
            this, &PlayerEngine::onNativeRenderingFailed);
}
updateNativeVideoRenderingEnabled();
```

Implement:

```cpp
void PlayerEngine::updateNativeVideoRenderingEnabled()
{
    const bool enabled = m_surface && m_surface->supportsNativeVideoToolboxRendering();
    if (m_nativeVideoRenderingEnabled == enabled)
        return;
    m_nativeVideoRenderingEnabled = enabled;
    m_decoder->setVideoToolboxDirectRenderingEnabled(enabled);
}
```

Implement failure handling:

```cpp
void PlayerEngine::onNativeRenderingFailed()
{
    if (!m_nativeVideoRenderingEnabled)
        return;
    m_nativeVideoRenderingEnabled = false;
    m_decoder->setVideoToolboxDirectRenderingEnabled(false);
    LOG_WARN("PlayerEngine: disabled VideoToolbox native rendering after Surface failure");
}
```

- [ ] **Step 2: Reset decoder native mode on close**

In `FFmpegDecoder::closeInternal()`, add:

```cpp
m_videoToolboxDirectRenderingEnabled = false;
```

- [ ] **Step 3: Count native fallback attempts in Surface**

In `FFmpegSurface.cpp`, when `setNativeFrame()` returns false for a VideoToolbox native frame, log a throttled warning:

```cpp
qWarning("FFmpegSurface: native VideoToolbox texture creation failed, frame dropped to fallback path.");
```

The warning should be guarded by a small counter so it emits at most three times per `VideoNode`.

Emit the failure signal:

```cpp
emit nativeRenderingFailed();
```

- [ ] **Step 4: Run regression script**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: PASS.

- [ ] **Step 5: Commit fallback and observability**

Run:

```bash
git add plugins/PlayPlugin/src/FFmpegDecoder.h plugins/PlayPlugin/src/FFmpegDecoder.cpp plugins/PlayPlugin/src/FFmpegSurface.cpp tests/playplugin_regression_checks.py
git add plugins/PlayPlugin/src/PlayerEngine.h plugins/PlayPlugin/src/PlayerEngine.cpp
git commit -m "[功能修改] 增加原生纹理回退观测"
```

Expected: commit succeeds.

### Task 8: Final Verification

**Files:**
- Verify: full project

- [ ] **Step 1: Run static regression checks**

Run:

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: exit code 0.

- [ ] **Step 2: Run full build**

Run:

```bash
cmake --build build --parallel
```

Expected: exit code 0, including `.mm` compilation and PlayPlugin linking.

- [ ] **Step 3: Run the app manually**

Run:

```bash
./build/app/VideoPlayerApp.app/Contents/MacOS/VideoPlayerApp
```

Expected: app launches. Play an H.264 or HEVC file on macOS. The video should render without green/purple tint, over-bright P010, or UV inversion.

- [ ] **Step 4: Inspect playback logs**

Expected log evidence:

```text
PlayPerf: decoder ... native=...
```

The native counter should increase for VideoToolbox playback. Hardware transfer counters should decrease relative to the CPU fallback path.

- [ ] **Step 5: Commit verification note if docs changed**

If any verification documentation is updated, run:

```bash
git add docs/superpowers/specs/2026-06-03-videotoolbox-metal-zero-copy-design.md docs/superpowers/plans/2026-06-03-videotoolbox-metal-zero-copy.md
git commit -m "[测试] 记录VideoToolbox零拷贝验证计划"
```

Expected: commit succeeds when docs changed; skip this step when no docs changed.
