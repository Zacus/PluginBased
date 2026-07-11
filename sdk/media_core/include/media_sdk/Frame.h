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
    VideoFrame();
    explicit VideoFrame(const VideoFrameDesc& desc);

    int width() const;
    int height() const;
    PixelFormat pixelFormat() const;
    ColorRange colorRange() const;
    ColorSpace colorSpace() const;
    std::chrono::microseconds pts() const;
    std::span<const PlaneView> planes() const;
    NativeHandle nativeHandle() const;
    bool hasStorage() const;
    std::shared_ptr<void> storage() const;

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
    AudioFrame();
    explicit AudioFrame(const AudioFrameDesc& desc);

    static AudioFrame fromOwnedSamples(AudioSampleFormat sampleFormat,
                                       int sampleRate,
                                       int channels,
                                       std::chrono::microseconds pts,
                                       std::vector<std::byte> samples);

    AudioSampleFormat sampleFormat() const;
    int sampleRate() const;
    int channels() const;
    std::chrono::microseconds pts() const;
    std::span<const std::byte> samples() const;
    bool hasStorage() const;

private:
    AudioSampleFormat m_sampleFormat = AudioSampleFormat::Unknown;
    int m_sampleRate = 0;
    int m_channels = 0;
    std::chrono::microseconds m_pts { 0 };
    std::vector<std::byte> m_samples;
    std::shared_ptr<void> m_storage;
};

} // namespace media_sdk
