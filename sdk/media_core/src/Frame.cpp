#include "media_sdk/Frame.h"

#include <utility>

namespace media_sdk {

VideoFrame::VideoFrame() = default;

VideoFrame::VideoFrame(const VideoFrameDesc& desc)
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

int VideoFrame::width() const
{
    return m_width;
}

int VideoFrame::height() const
{
    return m_height;
}

PixelFormat VideoFrame::pixelFormat() const
{
    return m_pixelFormat;
}

ColorRange VideoFrame::colorRange() const
{
    return m_colorRange;
}

ColorSpace VideoFrame::colorSpace() const
{
    return m_colorSpace;
}

std::chrono::microseconds VideoFrame::pts() const
{
    return m_pts;
}

std::span<const PlaneView> VideoFrame::planes() const
{
    return m_planes;
}

NativeHandle VideoFrame::nativeHandle() const
{
    return m_nativeHandle;
}

bool VideoFrame::hasStorage() const
{
    return static_cast<bool>(m_storage);
}

std::shared_ptr<void> VideoFrame::storage() const
{
    return m_storage;
}

AudioFrame::AudioFrame() = default;

AudioFrame::AudioFrame(const AudioFrameDesc& desc)
    : m_sampleFormat(desc.sampleFormat)
    , m_sampleRate(desc.sampleRate)
    , m_channels(desc.channels)
    , m_pts(desc.pts)
    , m_samples(desc.samples.begin(), desc.samples.end())
    , m_storage(desc.storage)
{
}

AudioFrame AudioFrame::fromOwnedSamples(AudioSampleFormat sampleFormat,
                                        int sampleRate,
                                        int channels,
                                        std::chrono::microseconds pts,
                                        std::vector<std::byte> samples)
{
    AudioFrame frame;
    frame.m_sampleFormat = sampleFormat;
    frame.m_sampleRate = sampleRate;
    frame.m_channels = channels;
    frame.m_pts = pts;
    frame.m_samples = std::move(samples);
    return frame;
}

AudioSampleFormat AudioFrame::sampleFormat() const
{
    return m_sampleFormat;
}

int AudioFrame::sampleRate() const
{
    return m_sampleRate;
}

int AudioFrame::channels() const
{
    return m_channels;
}

std::chrono::microseconds AudioFrame::pts() const
{
    return m_pts;
}

std::span<const std::byte> AudioFrame::samples() const
{
    return m_samples;
}

bool AudioFrame::hasStorage() const
{
    return static_cast<bool>(m_storage);
}

} // namespace media_sdk
