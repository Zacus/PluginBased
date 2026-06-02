# Hardware Decoder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 `plugins/PlayPlugin` 增加跨平台硬解码架构，第一阶段在 macOS 使用 VideoToolbox，并保持现有 CPU 帧渲染链路。

**Architecture:** `FFmpegDecoder` 只依赖一个 FFmpeg 中心化的 `HardwareDecoderBackend` 接口，平台选择集中在 `HardwareDecoderFactory`。macOS 后端配置 FFmpeg VideoToolbox 并把硬件帧 transfer 回 CPU `AVFrame`，Windows/Linux 第一阶段只有明确不可用的骨架。失败路径必须回退软件解码，播放不能因为硬解不可用而失败。

**Tech Stack:** Qt 6, C++17, FFmpeg `libavcodec` / `libavutil` hardware context APIs, CMake, existing `tests/playplugin_regression_checks.py`.

---

## 文件结构

- Create: `plugins/PlayPlugin/src/hw/HardwareDecoderBackend.h`
  - 定义后端接口、`std::unique_ptr` 类型别名、`AVHWDeviceContext` 资源释放方式。
- Create: `plugins/PlayPlugin/src/hw/HardwareDecoderFactory.h`
  - 声明 `createHardwareDecoderBackend(const AVCodec* codec, AVCodecID codecId)`。
- Create: `plugins/PlayPlugin/src/hw/HardwareDecoderFactory.cpp`
  - 集中平台选择：Apple 尝试 VideoToolbox，Windows/Linux 返回不可用骨架或空指针。
- Create: `plugins/PlayPlugin/src/hw/VideoToolboxBackend.h`
  - 声明 macOS VideoToolbox 后端。
- Create: `plugins/PlayPlugin/src/hw/VideoToolboxBackend.cpp`
  - 创建 `AV_HWDEVICE_TYPE_VIDEOTOOLBOX` 设备，通过 `get_format` 选择 `AV_PIX_FMT_VIDEOTOOLBOX`，transfer 到 CPU。
- Create: `plugins/PlayPlugin/src/hw/D3D11VABackend.h`
  - Windows D3D11VA 骨架。
- Create: `plugins/PlayPlugin/src/hw/D3D11VABackend.cpp`
  - 第一阶段明确返回不可用，不配置 codec context。
- Create: `plugins/PlayPlugin/src/hw/VaapiBackend.h`
  - Linux VAAPI 骨架。
- Create: `plugins/PlayPlugin/src/hw/VaapiBackend.cpp`
  - 第一阶段明确返回不可用，不配置 codec context。
- Modify: `plugins/PlayPlugin/src/FFmpegDecoder.h`
  - 增加硬解后端成员、打开视频解码器辅助函数、硬件帧处理辅助函数、transfer 失败节流计数。
- Modify: `plugins/PlayPlugin/src/FFmpegDecoder.cpp`
  - 视频解码器打开流程先尝试硬解，失败后重建 `AVCodecContext` 并软件回退；收帧后先 transfer 再 normalize。
- Modify: `plugins/PlayPlugin/CMakeLists.txt`
  - 把 `src/hw/*` 文件加入 `PlayPlugin` target。
- Modify: `tests/playplugin_regression_checks.py`
  - 增加结构回归检查，覆盖抽象、平台选择、回退、transfer 顺序和日志约束。

---

## 任务拆分

### Task 1: 建立硬解目录和接口壳

**Files:**
- Create: `plugins/PlayPlugin/src/hw/HardwareDecoderBackend.h`
- Modify: `plugins/PlayPlugin/CMakeLists.txt`
- Modify: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: 写静态回归检查，先要求接口文件存在**

在 `tests/playplugin_regression_checks.py` 的 `main()` 中加入读取和断言：

```python
    hw_backend_h = read("plugins/PlayPlugin/src/hw/HardwareDecoderBackend.h")
    cmake = read("plugins/PlayPlugin/CMakeLists.txt")

    require("class HardwareDecoderBackend" in hw_backend_h,
            "hardware decoder backend interface should exist")
    require("virtual QString name() const = 0" in hw_backend_h,
            "hardware backend should expose a stable log name")
    require("virtual bool isAvailableForCodec" in hw_backend_h,
            "hardware backend should decide codec availability")
    require("virtual bool configureContext(AVCodecContext* codecContext) = 0" in hw_backend_h,
            "hardware backend should configure AVCodecContext before avcodec_open2")
    require("virtual bool isHardwareFrame(const AVFrame* frame) const = 0" in hw_backend_h,
            "hardware backend should identify frames that need transfer")
    require("virtual AVFramePtr transferToCpuFrame(const AVFrame* frame) = 0" in hw_backend_h,
            "hardware backend should transfer hardware frames to CPU frames")
    require("src/hw/HardwareDecoderBackend.h" in cmake,
            "PlayPlugin target should include hardware backend interface")
```

- [ ] **Step 2: 运行检查确认失败**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: FAIL，错误信息包含 `HardwareDecoderBackend.h`。

- [ ] **Step 3: 创建接口文件**

Create `plugins/PlayPlugin/src/hw/HardwareDecoderBackend.h`:

```cpp
#pragma once

#include "../FFmpegUtils.h"

#include <QString>

class HardwareDecoderBackend
{
public:
    virtual ~HardwareDecoderBackend() = default;

    virtual QString name() const = 0;
    virtual bool isAvailableForCodec(const AVCodec* codec, AVCodecID codecId) const = 0;
    virtual bool configureContext(AVCodecContext* codecContext) = 0;
    virtual bool isHardwareFrame(const AVFrame* frame) const = 0;
    virtual AVFramePtr transferToCpuFrame(const AVFrame* frame) = 0;
    virtual void reset() = 0;
};
```

- [ ] **Step 4: 把接口头加入 CMake 源列表**

在 `plugins/PlayPlugin/CMakeLists.txt` 的 `SOURCES` 中 `src/FFmpegDecoder.h src/FFmpegDecoder.cpp` 后加入：

```cmake
        src/hw/HardwareDecoderBackend.h
```

- [ ] **Step 5: 验证**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: PASS。

Run: `cmake --build build --parallel`

Expected: build 完成，`PlayPlugin` target 无编译错误。

- [ ] **Step 6: 提交**

```bash
git add plugins/PlayPlugin/src/hw/HardwareDecoderBackend.h \
        plugins/PlayPlugin/CMakeLists.txt \
        tests/playplugin_regression_checks.py
git commit -m "[功能新增] 增加硬解码后端接口"
```

---

### Task 2: 增加平台后端骨架和工厂

**Files:**
- Create: `plugins/PlayPlugin/src/hw/HardwareDecoderFactory.h`
- Create: `plugins/PlayPlugin/src/hw/HardwareDecoderFactory.cpp`
- Create: `plugins/PlayPlugin/src/hw/VideoToolboxBackend.h`
- Create: `plugins/PlayPlugin/src/hw/VideoToolboxBackend.cpp`
- Create: `plugins/PlayPlugin/src/hw/D3D11VABackend.h`
- Create: `plugins/PlayPlugin/src/hw/D3D11VABackend.cpp`
- Create: `plugins/PlayPlugin/src/hw/VaapiBackend.h`
- Create: `plugins/PlayPlugin/src/hw/VaapiBackend.cpp`
- Modify: `plugins/PlayPlugin/CMakeLists.txt`
- Modify: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: 写回归检查，要求工厂和三个后端存在**

在 `tests/playplugin_regression_checks.py` 中读取新增文件：

```python
    hw_factory_h = read("plugins/PlayPlugin/src/hw/HardwareDecoderFactory.h")
    hw_factory_cpp = read("plugins/PlayPlugin/src/hw/HardwareDecoderFactory.cpp")
    videotoolbox_cpp = read("plugins/PlayPlugin/src/hw/VideoToolboxBackend.cpp")
    d3d11va_cpp = read("plugins/PlayPlugin/src/hw/D3D11VABackend.cpp")
    vaapi_cpp = read("plugins/PlayPlugin/src/hw/VaapiBackend.cpp")
```

加入断言：

```python
    require("createHardwareDecoderBackend" in hw_factory_h and
            "std::unique_ptr<HardwareDecoderBackend>" in hw_factory_h,
            "hardware decoder factory should return an optional backend")
    require("#if defined(Q_OS_APPLE)" in hw_factory_cpp and "VideoToolboxBackend" in hw_factory_cpp,
            "factory should select VideoToolbox only on Apple platforms")
    require("#if defined(Q_OS_WIN)" in hw_factory_cpp and "D3D11VABackend" in hw_factory_cpp,
            "factory should know the Windows skeleton backend")
    require("#if defined(Q_OS_LINUX)" in hw_factory_cpp and "VaapiBackend" in hw_factory_cpp,
            "factory should know the Linux skeleton backend")
    require("return false;" in d3d11va_cpp and "d3d11va" in d3d11va_cpp,
            "D3D11VA backend should be explicitly unavailable in phase 1")
    require("return false;" in vaapi_cpp and "vaapi" in vaapi_cpp,
            "VAAPI backend should be explicitly unavailable in phase 1")
    require("videotoolbox" in videotoolbox_cpp,
            "VideoToolbox backend should expose a stable backend name")
```

- [ ] **Step 2: 运行检查确认失败**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: FAIL，错误信息包含缺失的 `HardwareDecoderFactory.h`。

- [ ] **Step 3: 创建工厂头文件**

Create `plugins/PlayPlugin/src/hw/HardwareDecoderFactory.h`:

```cpp
#pragma once

#include "HardwareDecoderBackend.h"

#include <memory>

std::unique_ptr<HardwareDecoderBackend> createHardwareDecoderBackend(const AVCodec* codec,
                                                                     AVCodecID codecId);
```

- [ ] **Step 4: 创建三个后端头文件**

`VideoToolboxBackend.h`:

```cpp
#pragma once

#include "HardwareDecoderBackend.h"

class VideoToolboxBackend final : public HardwareDecoderBackend
{
public:
    QString name() const override;
    bool isAvailableForCodec(const AVCodec* codec, AVCodecID codecId) const override;
    bool configureContext(AVCodecContext* codecContext) override;
    bool isHardwareFrame(const AVFrame* frame) const override;
    AVFramePtr transferToCpuFrame(const AVFrame* frame) override;
    void reset() override;
};
```

`D3D11VABackend.h`:

```cpp
#pragma once

#include "HardwareDecoderBackend.h"

class D3D11VABackend final : public HardwareDecoderBackend
{
public:
    QString name() const override;
    bool isAvailableForCodec(const AVCodec* codec, AVCodecID codecId) const override;
    bool configureContext(AVCodecContext* codecContext) override;
    bool isHardwareFrame(const AVFrame* frame) const override;
    AVFramePtr transferToCpuFrame(const AVFrame* frame) override;
    void reset() override;
};
```

`VaapiBackend.h`:

```cpp
#pragma once

#include "HardwareDecoderBackend.h"

class VaapiBackend final : public HardwareDecoderBackend
{
public:
    QString name() const override;
    bool isAvailableForCodec(const AVCodec* codec, AVCodecID codecId) const override;
    bool configureContext(AVCodecContext* codecContext) override;
    bool isHardwareFrame(const AVFrame* frame) const override;
    AVFramePtr transferToCpuFrame(const AVFrame* frame) override;
    void reset() override;
};
```

- [ ] **Step 5: 创建骨架实现**

`D3D11VABackend.cpp`:

```cpp
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
```

`VaapiBackend.cpp` 使用同样结构，`name()` 返回 `QStringLiteral("vaapi")`。

`VideoToolboxBackend.cpp` 第一版只提供可编译骨架，真实配置在 Task 5：

```cpp
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
```

- [ ] **Step 6: 创建工厂实现**

Create `plugins/PlayPlugin/src/hw/HardwareDecoderFactory.cpp`:

```cpp
#include "HardwareDecoderFactory.h"

#include <QtGlobal>

#if defined(Q_OS_APPLE)
#include "VideoToolboxBackend.h"
#endif
#if defined(Q_OS_WIN)
#include "D3D11VABackend.h"
#endif
#if defined(Q_OS_LINUX)
#include "VaapiBackend.h"
#endif

std::unique_ptr<HardwareDecoderBackend> createHardwareDecoderBackend(const AVCodec* codec,
                                                                     AVCodecID codecId)
{
#if defined(Q_OS_APPLE)
    auto backend = std::make_unique<VideoToolboxBackend>();
    if (backend->isAvailableForCodec(codec, codecId))
        return backend;
#endif

#if defined(Q_OS_WIN)
    auto backend = std::make_unique<D3D11VABackend>();
    if (backend->isAvailableForCodec(codec, codecId))
        return backend;
#endif

#if defined(Q_OS_LINUX)
    auto backend = std::make_unique<VaapiBackend>();
    if (backend->isAvailableForCodec(codec, codecId))
        return backend;
#endif

    return {};
}
```

- [ ] **Step 7: 把新增文件加入 CMake**

在 `plugins/PlayPlugin/CMakeLists.txt` 的 `SOURCES` 中加入：

```cmake
        src/hw/HardwareDecoderFactory.h src/hw/HardwareDecoderFactory.cpp
        src/hw/VideoToolboxBackend.h    src/hw/VideoToolboxBackend.cpp
        src/hw/D3D11VABackend.h         src/hw/D3D11VABackend.cpp
        src/hw/VaapiBackend.h           src/hw/VaapiBackend.cpp
```

- [ ] **Step 8: 验证**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: PASS。

Run: `cmake --build build --parallel`

Expected: build 完成。此时 VideoToolbox 仍然不会启用，行为应等同软件解码。

- [ ] **Step 9: 提交**

```bash
git add plugins/PlayPlugin/src/hw \
        plugins/PlayPlugin/CMakeLists.txt \
        tests/playplugin_regression_checks.py
git commit -m "[功能新增] 增加硬解码平台后端骨架"
```

---

### Task 3: 把 FFmpegDecoder 接到工厂但保持软件路径

**Files:**
- Modify: `plugins/PlayPlugin/src/FFmpegDecoder.h`
- Modify: `plugins/PlayPlugin/src/FFmpegDecoder.cpp`
- Modify: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: 写检查，要求解码器只通过工厂接触硬解**

加入断言：

```python
    require('#include "hw/HardwareDecoderFactory.h"' in decoder_cpp,
            "FFmpegDecoder should include the hardware backend factory")
    require("std::unique_ptr<HardwareDecoderBackend> m_hardwareDecoder" in decoder_h,
            "FFmpegDecoder should own the selected hardware backend")
    require("createHardwareDecoderBackend(vcodec, vs->codecpar->codec_id)" in decoder_cpp,
            "FFmpegDecoder should ask the factory for video hardware decoding")
    require("m_hardwareDecoder.reset();" in decoder_cpp[decoder_cpp.find("void FFmpegDecoder::closeInternal"):],
            "FFmpegDecoder should release hardware backend on close")
    require("Q_OS_APPLE" not in decoder_cpp and
            "Q_OS_WIN" not in decoder_cpp and
            "Q_OS_LINUX" not in decoder_cpp,
            "FFmpegDecoder should not contain platform branching for hardware backend selection")
```

- [ ] **Step 2: 运行检查确认失败**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: FAIL，错误信息包含 `FFmpegDecoder should include the hardware backend factory`。

- [ ] **Step 3: 修改头文件成员**

在 `plugins/PlayPlugin/src/FFmpegDecoder.h` 中加入：

```cpp
#include "hw/HardwareDecoderBackend.h"

#include <memory>
```

在 FFmpeg 上下文成员区域加入：

```cpp
    std::unique_ptr<HardwareDecoderBackend> m_hardwareDecoder;
```

- [ ] **Step 4: 在视频流打开处选择后端但不改变打开行为**

在 `plugins/PlayPlugin/src/FFmpegDecoder.cpp` 顶部加入：

```cpp
#include "hw/HardwareDecoderFactory.h"
```

在找到 `vcodec` 且分配 `vctx` 后、`avcodec_open2()` 前加入：

```cpp
            m_hardwareDecoder = createHardwareDecoderBackend(vcodec, vs->codecpar->codec_id);
            if (m_hardwareDecoder)
            {
                LOG_INFO("FFmpegDecoder: hardware decoder candidate {}",
                         m_hardwareDecoder->name().toStdString());
            }
```

此任务不调用 `configureContext()`，因为 VideoToolbox 实现还没有真实启用。

- [ ] **Step 5: close 时释放后端**

在 `FFmpegDecoder::closeInternal()` 中 `m_videoCodecCtx.reset();` 前加入：

```cpp
    if (m_hardwareDecoder)
        m_hardwareDecoder->reset();
    m_hardwareDecoder.reset();
```

- [ ] **Step 6: 验证**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: PASS。

Run: `cmake --build build --parallel`

Expected: build 完成。打开任意已有视频时行为仍走软件解码。

- [ ] **Step 7: 提交**

```bash
git add plugins/PlayPlugin/src/FFmpegDecoder.h \
        plugins/PlayPlugin/src/FFmpegDecoder.cpp \
        tests/playplugin_regression_checks.py
git commit -m "[功能新增] 接入硬解码后端工厂"
```

---

### Task 4: 重构视频解码器打开流程以支持回退

**Files:**
- Modify: `plugins/PlayPlugin/src/FFmpegDecoder.h`
- Modify: `plugins/PlayPlugin/src/FFmpegDecoder.cpp`
- Modify: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: 写检查，要求存在可重试的软件回退函数**

加入断言：

```python
    require("bool openVideoCodec(AVStream* stream, const AVCodec* codec)" in decoder_h,
            "FFmpegDecoder should open video codec through a retryable helper")
    require("openVideoCodec(vs, vcodec)" in decoder_cpp,
            "openInternal should delegate video codec opening")
    require("configureContext(vctx)" in decoder_cpp,
            "video codec helper should configure hardware before avcodec_open2")
    require("fallback to software decoding" in decoder_cpp,
            "hardware open failure should log software fallback")
    require("avcodec_alloc_context3(codec)" in decoder_cpp and
            decoder_cpp.count("avcodec_parameters_to_context") >= 3,
            "software fallback should rebuild a clean AVCodecContext")
```

- [ ] **Step 2: 运行检查确认失败**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: FAIL，错误信息包含 `retryable helper`。

- [ ] **Step 3: 声明辅助函数**

在 `FFmpegDecoder.h` private 方法中加入：

```cpp
    bool openVideoCodec(AVStream* stream, const AVCodec* codec);
    AVCodecContext* createVideoCodecContext(AVStream* stream, const AVCodec* codec);
```

- [ ] **Step 4: 抽出 codec context 创建函数**

在 `FFmpegDecoder.cpp` 的 `openInternal()` 前加入：

```cpp
AVCodecContext* FFmpegDecoder::createVideoCodecContext(AVStream* stream, const AVCodec* codec)
{
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx)
        return nullptr;

    const int ret = avcodec_parameters_to_context(ctx, stream->codecpar);
    if (ret < 0)
    {
        LOG_WARN("FFmpegDecoder: avcodec_parameters_to_context for video failed: {}",
                 av_err(ret));
        avcodec_free_context(&ctx);
        return nullptr;
    }

    ctx->thread_count = 0;
    return ctx;
}
```

- [ ] **Step 5: 实现可回退的视频打开函数**

加入：

```cpp
bool FFmpegDecoder::openVideoCodec(AVStream* stream, const AVCodec* codec)
{
    AVCodecContext* vctx = createVideoCodecContext(stream, codec);
    if (!vctx)
        return false;

    m_hardwareDecoder = createHardwareDecoderBackend(codec, stream->codecpar->codec_id);
    if (m_hardwareDecoder)
    {
        LOG_INFO("FFmpegDecoder: selected hardware decoder {}",
                 m_hardwareDecoder->name().toStdString());
        if (!m_hardwareDecoder->configureContext(vctx))
        {
            LOG_WARN("FFmpegDecoder: hardware setup failed, fallback to software decoding");
            m_hardwareDecoder->reset();
            m_hardwareDecoder.reset();
        }
    }

    int ret = avcodec_open2(vctx, codec, nullptr);
    if (ret < 0 && m_hardwareDecoder)
    {
        LOG_WARN("FFmpegDecoder: hardware codec open failed: {}, fallback to software decoding",
                 av_err(ret));
        avcodec_free_context(&vctx);
        m_hardwareDecoder->reset();
        m_hardwareDecoder.reset();

        vctx = createVideoCodecContext(stream, codec);
        if (!vctx)
            return false;
        ret = avcodec_open2(vctx, codec, nullptr);
    }

    if (ret < 0)
    {
        avcodec_free_context(&vctx);
        LOG_WARN("FFmpegDecoder: failed to open video decoder: {}", av_err(ret));
        return false;
    }

    m_videoCodecCtx.reset(vctx);
    m_videoWidth = vctx->width;
    m_videoHeight = vctx->height;
    AVRational fr = stream->avg_frame_rate;
    m_videoFps = (fr.den > 0) ? av_q2d(fr) : 25.0;
    return true;
}
```

- [ ] **Step 6: 替换 openInternal 中的视频打开块**

将原来从 `AVCodecContext* vctx = avcodec_alloc_context3(vcodec);` 开始，到设置 `m_videoFps` 结束的视频 codec 打开逻辑整段替换为：

```cpp
            if (!openVideoCodec(vs, vcodec))
            {
                m_videoStreamIdx = -1;
            }
```

- [ ] **Step 7: 验证**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: PASS。

Run: `cmake --build build --parallel`

Expected: build 完成。因为 Task 5 尚未真实启用 VideoToolbox，此时仍应自动回退软件解码。

- [ ] **Step 8: 提交**

```bash
git add plugins/PlayPlugin/src/FFmpegDecoder.h \
        plugins/PlayPlugin/src/FFmpegDecoder.cpp \
        tests/playplugin_regression_checks.py
git commit -m "[功能修改] 支持硬解码失败回退"
```

---

### Task 5: 实现 macOS VideoToolbox 配置

**Files:**
- Modify: `plugins/PlayPlugin/src/FFmpegUtils.h`
- Modify: `plugins/PlayPlugin/src/hw/VideoToolboxBackend.h`
- Modify: `plugins/PlayPlugin/src/hw/VideoToolboxBackend.cpp`
- Modify: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: 写检查，要求 VideoToolbox 使用 FFmpeg 硬件 API**

加入断言：

```python
    ffmpeg_utils_h = read("plugins/PlayPlugin/src/FFmpegUtils.h")
    videotoolbox_h = read("plugins/PlayPlugin/src/hw/VideoToolboxBackend.h")

    require("#include <libavutil/hwcontext.h>" in ffmpeg_utils_h,
            "FFmpegUtils should expose FFmpeg hardware context APIs")
    require("AVBufferRefPtr" in ffmpeg_utils_h,
            "FFmpegUtils should provide RAII for AVBufferRef")
    require("AVBufferRefPtr m_deviceContext" in videotoolbox_h,
            "VideoToolbox backend should own the hardware device context")
    require("av_hwdevice_ctx_create" in videotoolbox_cpp and
            "AV_HWDEVICE_TYPE_VIDEOTOOLBOX" in videotoolbox_cpp,
            "VideoToolbox backend should create a VideoToolbox hardware device")
    require("avcodec_get_hw_config" in videotoolbox_cpp and
            "AV_PIX_FMT_VIDEOTOOLBOX" in videotoolbox_cpp,
            "VideoToolbox backend should verify decoder hardware config")
    require("selectVideoToolboxFormat" in videotoolbox_cpp and
            "codecContext->get_format = selectVideoToolboxFormat" in videotoolbox_cpp,
            "VideoToolbox backend should force FFmpeg to choose the hardware pixel format")
    require("codecContext->hw_device_ctx = av_buffer_ref" in videotoolbox_cpp,
            "VideoToolbox backend should attach hardware device to AVCodecContext")
```

- [ ] **Step 2: 运行检查确认失败**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: FAIL，错误信息包含 `hardware context APIs`。

- [ ] **Step 3: 在 FFmpegUtils 增加 AVBufferRef RAII**

在 extern "C" block 内加入：

```cpp
#include <libavutil/buffer.h>
#include <libavutil/hwcontext.h>
```

在 deleter 区域加入：

```cpp
struct AVBufferRefDeleter {
    void operator()(AVBufferRef* ref) const {
        if (ref) av_buffer_unref(&ref);
    }
};
```

在类型别名区域加入：

```cpp
using AVBufferRefPtr = std::unique_ptr<AVBufferRef, AVBufferRefDeleter>;
```

- [ ] **Step 4: 给 VideoToolboxBackend 增加设备成员和目标格式**

在 `VideoToolboxBackend.h` private 区域加入：

```cpp
private:
    AVBufferRefPtr m_deviceContext;
```

- [ ] **Step 5: 实现 VideoToolbox 可用性检查和配置**

将 `VideoToolboxBackend.cpp` 改为：

```cpp
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

AVFramePtr VideoToolboxBackend::transferToCpuFrame(const AVFrame*)
{
    return {};
}

void VideoToolboxBackend::reset()
{
    m_deviceContext.reset();
}
```

- [ ] **Step 6: 验证**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: PASS。

Run: `cmake --build build --parallel`

Expected: build 完成。打开 HEVC/H264 视频时日志应出现 `selected hardware decoder videotoolbox` 或软件回退日志。

- [ ] **Step 7: 提交**

```bash
git add plugins/PlayPlugin/src/FFmpegUtils.h \
        plugins/PlayPlugin/src/hw/VideoToolboxBackend.h \
        plugins/PlayPlugin/src/hw/VideoToolboxBackend.cpp \
        tests/playplugin_regression_checks.py
git commit -m "[功能新增] 启用VideoToolbox硬解配置"
```

---

### Task 6: transfer 硬件帧到 CPU 帧

**Files:**
- Modify: `plugins/PlayPlugin/src/hw/VideoToolboxBackend.cpp`
- Modify: `plugins/PlayPlugin/src/FFmpegDecoder.h`
- Modify: `plugins/PlayPlugin/src/FFmpegDecoder.cpp`
- Modify: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: 写检查，要求 transfer 先于 normalize**

加入断言：

```python
    require("av_hwframe_transfer_data" in videotoolbox_cpp,
            "VideoToolbox backend should transfer hardware frames to CPU frames")
    require("copyFrameMetadata" in decoder_cpp,
            "FFmpegDecoder should preserve timing and color metadata after hardware transfer")
    require("prepareVideoFrameForQueue" in decoder_h and "prepareVideoFrameForQueue(std::move(frame))" in decoder_cpp,
            "FFmpegDecoder should prepare video frames through a single helper before queueing")
    prepare_body = decoder_cpp[decoder_cpp.find("AVFramePtr FFmpegDecoder::prepareVideoFrameForQueue"):
                               decoder_cpp.find("AVFramePtr FFmpegDecoder::normalizeVideoFrame")]
    require("transferHardwareFrameToCpu" in prepare_body and
            prepare_body.find("transferHardwareFrameToCpu") < prepare_body.find("normalizeVideoFrame"),
            "hardware frames should be transferred before normalizeVideoFrame")
```

- [ ] **Step 2: 运行检查确认失败**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: FAIL，错误信息包含 `transfer hardware frames`。

- [ ] **Step 3: 实现 VideoToolbox transfer**

替换 `VideoToolboxBackend::transferToCpuFrame`：

```cpp
AVFramePtr VideoToolboxBackend::transferToCpuFrame(const AVFrame* frame)
{
    if (!isHardwareFrame(frame))
        return {};

    auto cpuFrame = make_frame();
    const int ret = av_hwframe_transfer_data(cpuFrame.get(), frame, 0);
    if (ret < 0)
    {
        LOG_WARN("VideoToolboxBackend: av_hwframe_transfer_data failed: {}", av_err(ret));
        return {};
    }

    return cpuFrame;
}
```

- [ ] **Step 4: 声明解码器辅助函数**

在 `FFmpegDecoder.h` private 方法中加入：

```cpp
    AVFramePtr prepareVideoFrameForQueue(AVFramePtr frame);
    AVFramePtr transferHardwareFrameToCpu(AVFramePtr frame);
    void copyFrameMetadata(const AVFrame* source, AVFrame* destination) const;
```

- [ ] **Step 5: 实现 metadata 复制和硬件帧准备**

在 `FFmpegDecoder.cpp` 中 `sendPacketToDecoder()` 后、`normalizeVideoFrame()` 前加入：

```cpp
void FFmpegDecoder::copyFrameMetadata(const AVFrame* source, AVFrame* destination) const
{
    destination->pts = source->pts;
    destination->sample_aspect_ratio = source->sample_aspect_ratio;
    destination->color_range = source->color_range;
    destination->colorspace = source->colorspace;
    destination->color_primaries = source->color_primaries;
    destination->color_trc = source->color_trc;
    destination->chroma_location = source->chroma_location;
}

AVFramePtr FFmpegDecoder::transferHardwareFrameToCpu(AVFramePtr frame)
{
    if (!m_hardwareDecoder || !m_hardwareDecoder->isHardwareFrame(frame.get()))
        return frame;

    AVFramePtr cpuFrame = m_hardwareDecoder->transferToCpuFrame(frame.get());
    if (!cpuFrame)
        return {};

    copyFrameMetadata(frame.get(), cpuFrame.get());
    return cpuFrame;
}

AVFramePtr FFmpegDecoder::prepareVideoFrameForQueue(AVFramePtr frame)
{
    frame = transferHardwareFrameToCpu(std::move(frame));
    if (!frame)
        return {};
    return normalizeVideoFrame(std::move(frame));
}
```

- [ ] **Step 6: 替换 sendPacketToDecoder 中的视频处理入口**

把：

```cpp
            frame = normalizeVideoFrame(std::move(frame));
```

替换为：

```cpp
            frame = prepareVideoFrameForQueue(std::move(frame));
```

- [ ] **Step 7: 验证**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: PASS。

Run: `cmake --build build --parallel`

Expected: build 完成。打开 `6月29日.mov` 时如果 VideoToolbox 成功，视频应持续推进，不应只显示首帧。

- [ ] **Step 8: 提交**

```bash
git add plugins/PlayPlugin/src/hw/VideoToolboxBackend.cpp \
        plugins/PlayPlugin/src/FFmpegDecoder.h \
        plugins/PlayPlugin/src/FFmpegDecoder.cpp \
        tests/playplugin_regression_checks.py
git commit -m "[功能新增] 转换硬解帧到CPU渲染路径"
```

---

### Task 7: 节流 transfer 失败日志并限制单媒体失败影响

**Files:**
- Modify: `plugins/PlayPlugin/src/FFmpegDecoder.h`
- Modify: `plugins/PlayPlugin/src/FFmpegDecoder.cpp`
- Modify: `plugins/PlayPlugin/src/hw/VideoToolboxBackend.cpp`
- Modify: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: 写检查，要求失败日志不逐帧刷屏**

加入断言：

```python
    require("m_hardwareTransferFailureCount" in decoder_h,
            "FFmpegDecoder should count hardware transfer failures")
    require("MaxHardwareTransferFailureLogs" in decoder_cpp,
            "hardware transfer failure logs should be throttled")
    require("hardware frame transfer failed" in decoder_cpp,
            "decoder should log hardware transfer failures at the decode boundary")
    require("LOG_WARN(\"VideoToolboxBackend: av_hwframe_transfer_data failed" not in videotoolbox_cpp,
            "backend should not emit one warning for every failed transfer")
```

- [ ] **Step 2: 运行检查确认失败**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: FAIL，错误信息包含 `hardware transfer failures`。

- [ ] **Step 3: 增加计数成员**

在 `FFmpegDecoder.h` FFmpeg 上下文成员区域加入：

```cpp
    int m_hardwareTransferFailureCount = 0;
```

- [ ] **Step 4: 在 cpp 中增加日志上限**

在 `isRendererSupportedVideoFormat()` 后加入：

```cpp
constexpr int MaxHardwareTransferFailureLogs = 3;
```

- [ ] **Step 5: 修改 transfer 失败处理**

把 `transferHardwareFrameToCpu()` 中 `if (!cpuFrame) return {};` 替换为：

```cpp
    if (!cpuFrame)
    {
        ++m_hardwareTransferFailureCount;
        if (m_hardwareTransferFailureCount <= MaxHardwareTransferFailureLogs)
        {
            LOG_WARN("FFmpegDecoder: hardware frame transfer failed, dropping frame");
        }
        return {};
    }
```

在成功 transfer 后加入：

```cpp
    m_hardwareTransferFailureCount = 0;
```

- [ ] **Step 6: 移除 VideoToolboxBackend transfer 中的逐次 warning**

把 `VideoToolboxBackend::transferToCpuFrame()` 里的失败日志改成直接返回空：

```cpp
    if (ret < 0)
        return {};
```

- [ ] **Step 7: close 时重置计数**

在 `closeInternal()` 中加入：

```cpp
    m_hardwareTransferFailureCount = 0;
```

- [ ] **Step 8: 验证**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: PASS。

Run: `cmake --build build --parallel`

Expected: build 完成。手动播放样本时，transfer 失败最多连续记录 3 次同类 warning，然后继续尝试后续帧。

- [ ] **Step 9: 提交**

```bash
git add plugins/PlayPlugin/src/FFmpegDecoder.h \
        plugins/PlayPlugin/src/FFmpegDecoder.cpp \
        plugins/PlayPlugin/src/hw/VideoToolboxBackend.cpp \
        tests/playplugin_regression_checks.py
git commit -m "[功能修改] 节流硬解帧转换失败日志"
```

---

### Task 8: 手动验证样本和普通视频

**Files:**
- Modify: none

- [ ] **Step 1: 运行结构检查**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: PASS。

- [ ] **Step 2: 构建**

Run: `cmake --build build --parallel`

Expected: build 完成。

- [ ] **Step 3: 启动应用**

Run: `./build/app/VideoPlayerApp`

Expected: 应用启动，PlayPlugin 可加载，控制台或日志无 plugin 加载失败。

- [ ] **Step 4: 播放普通非 HEVC 视频**

Expected:
- 视频可以打开。
- 音频和视频正常推进。
- 拖动进度条后仍能继续播放。

- [ ] **Step 5: 播放样本 `6月29日.mov`**

Expected:
- 不黑屏。
- 不停留在第一帧。
- 日志出现 `selected hardware decoder videotoolbox` 或清晰的软件回退原因。
- 如果硬解成功，画面推进明显比纯软件路径更稳定。
- 如果硬解不可用，应用仍能播放或按现有软件能力降帧，不因硬解缺失报错退出。

- [ ] **Step 6: 检查日志**

Run: `tail -n 200 "$HOME/Library/Application Support/MyOrg/VideoPlayer/logs/videoplayer.log"`

Expected:
- 不出现逐帧 `VideoRenderer: drop frame` debug 日志。
- 硬解 setup 成功或失败只有状态级日志。
- transfer 失败 warning 受到节流。

- [ ] **Step 7: 提交验证记录**

如果 Task 8 发现需要代码修复，先修复并重复 Step 1-6，再提交修复。若没有代码变化，不创建空提交。

---

## 执行顺序和可验证边界

1. Task 1 只建立接口，验证点是接口结构和可编译。
2. Task 2 只建立工厂和平台骨架，验证点是平台选择集中、非 macOS 后端明确不可用。
3. Task 3 只让 `FFmpegDecoder` 持有并选择后端，验证点是行为仍保持软件解码。
4. Task 4 改视频打开流程，验证点是硬解配置失败能重建干净 codec context 并回退。
5. Task 5 真正启用 VideoToolbox 配置，验证点是 macOS FFmpeg 硬件设备可创建或明确回退。
6. Task 6 接入硬件帧 transfer，验证点是 transfer 在 normalize 前完成，CPU 渲染链路不改。
7. Task 7 控制失败日志和单帧失败影响，验证点是不刷屏、不终止播放。
8. Task 8 做端到端手动验证，验证点是普通视频不回归，样本 MOV 不黑屏、不只停第一帧。

每个代码任务完成后都执行：

```bash
python3 tests/playplugin_regression_checks.py
cmake --build build --parallel
```

每个代码任务通过验证后按该任务的提交命令提交。需要推送时，在每个提交之后执行：

```bash
git push origin main
```

---

## 自检

- 设计稿目标已覆盖：跨平台抽象、macOS VideoToolbox 第一阶段可用、Windows/Linux 骨架不可用、CPU transfer、软件回退、日志节流、普通播放不回归。
- 没有引入 zero-copy、QRhi 纹理互操作、UI 改造或 Windows/Linux 完整实现。
- 风险保留在验证阶段暴露：本机 FFmpeg 是否带 VideoToolbox、`av_hwframe_transfer_data()` 成本是否足以改善 4K/60、HDR/HLG 色彩准确性不在本阶段解决。
