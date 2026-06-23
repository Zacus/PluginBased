#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace media_sdk {

enum class PixelFormat {
    Unknown,
    Yuv420P,
    Nv12,
    P010,
    Yuv420P10,
    Yuv422P10,
    Yuv444P10,
    Native
};

enum class ColorRange {
    Unknown,
    Limited,
    Full
};

enum class ColorSpace {
    Unknown,
    Bt601,
    Bt709
};

enum class NativeHandleKind {
    None,
    VideoToolboxPixelBuffer,
    D3D11Texture,
    VaapiSurface
};

struct PlaneView {
    const std::byte* data = nullptr;
    int stride = 0;
    int width = 0;
    int height = 0;
};

struct NativeHandle {
    NativeHandleKind kind = NativeHandleKind::None;
    void* handle = nullptr;
    int pixelFormat = 0;
};

struct VideoFrameDesc {
    int width = 0;
    int height = 0;
    PixelFormat pixelFormat = PixelFormat::Unknown;
    ColorRange colorRange = ColorRange::Unknown;
    ColorSpace colorSpace = ColorSpace::Unknown;
    std::chrono::microseconds pts { 0 };
    std::span<const PlaneView> planes {};
    NativeHandle nativeHandle {};
    std::shared_ptr<void> storage;
};

class VideoFrame
{
public:
    VideoFrame() = default;
    explicit VideoFrame(const VideoFrameDesc& desc)
        : m_width(desc.width)
        , m_height(desc.height)
        , m_pixelFormat(desc.pixelFormat)
        , m_colorRange(desc.colorRange)
        , m_colorSpace(desc.colorSpace)
        , m_pts(desc.pts)
        , m_planes(desc.planes.begin(), desc.planes.end())
        , m_nativeHandle(desc.nativeHandle)
        , m_storage(desc.storage)
    {
    }

    int width() const { return m_width; }
    int height() const { return m_height; }
    PixelFormat pixelFormat() const { return m_pixelFormat; }
    ColorRange colorRange() const { return m_colorRange; }
    ColorSpace colorSpace() const { return m_colorSpace; }
    std::chrono::microseconds pts() const { return m_pts; }
    std::span<const PlaneView> planes() const { return m_planes; }
    NativeHandle nativeHandle() const { return m_nativeHandle; }
    bool hasStorage() const { return static_cast<bool>(m_storage); }

private:
    int m_width = 0;
    int m_height = 0;
    PixelFormat m_pixelFormat = PixelFormat::Unknown;
    ColorRange m_colorRange = ColorRange::Unknown;
    ColorSpace m_colorSpace = ColorSpace::Unknown;
    std::chrono::microseconds m_pts { 0 };
    std::vector<PlaneView> m_planes;
    NativeHandle m_nativeHandle;
    std::shared_ptr<void> m_storage;
};

enum class AudioSampleFormat {
    Unknown,
    Float32Interleaved,
    Signed16Interleaved,
    Signed32Interleaved
};

struct AudioFrameDesc {
    AudioSampleFormat sampleFormat = AudioSampleFormat::Unknown;
    int sampleRate = 0;
    int channels = 0;
    std::chrono::microseconds pts { 0 };
    std::span<const std::byte> samples {};
    std::shared_ptr<void> storage;
};

class AudioFrame
{
public:
    AudioFrame() = default;
    explicit AudioFrame(const AudioFrameDesc& desc)
        : m_sampleFormat(desc.sampleFormat)
        , m_sampleRate(desc.sampleRate)
        , m_channels(desc.channels)
        , m_pts(desc.pts)
        , m_samples(desc.samples.begin(), desc.samples.end())
        , m_storage(desc.storage)
    {
    }

    AudioSampleFormat sampleFormat() const { return m_sampleFormat; }
    int sampleRate() const { return m_sampleRate; }
    int channels() const { return m_channels; }
    std::chrono::microseconds pts() const { return m_pts; }
    std::span<const std::byte> samples() const { return m_samples; }
    bool hasStorage() const { return static_cast<bool>(m_storage); }

private:
    AudioSampleFormat m_sampleFormat = AudioSampleFormat::Unknown;
    int m_sampleRate = 0;
    int m_channels = 0;
    std::chrono::microseconds m_pts { 0 };
    std::vector<std::byte> m_samples;
    std::shared_ptr<void> m_storage;
};

} // namespace media_sdk
