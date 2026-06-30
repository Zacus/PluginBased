#pragma once

#include "media_sdk/runtime/AudioOutput.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <vector>

namespace media_sdk::platform::macos {

struct CoreAudioRingBufferReadResult {
    std::size_t copiedBytes = 0;
    std::size_t silenceBytes = 0;
};

// Mutex-protected SPSC PCM ring buffer.
// The producer/runtime thread may block in write() for capacity. The CoreAudio
// callback path calls read(); read() never waits and returns silence on underrun.
// Stored data is interleaved PCM, and queue operations publish only complete PCM frames.
class CoreAudioRingBuffer
{
public:
    explicit CoreAudioRingBuffer(std::size_t capacityBytes);

    void configure(runtime::AudioFormat format, runtime::Generation generation);
    bool write(runtime::AudioBufferView buffer);
    CoreAudioRingBufferReadResult read(std::span<std::byte> destination);
    void flush();
    void close();

    runtime::ClockSnapshot clock() const;

private:
    std::size_t bytesPerSample() const;
    std::size_t bytesPerFrame() const;
    std::size_t completeFrameBytes(std::size_t bytes) const;
    std::chrono::microseconds durationForBytes(std::size_t bytes) const;
    void copyIntoRing(std::span<const std::byte> source);
    void copyFromRing(std::span<std::byte> destination);
    void resetBufferLocked();
    void updateSnapshotLocked();

    const std::size_t m_capacityBytes;
    mutable std::mutex m_mutex;
    std::condition_variable m_notFull;
    std::vector<std::byte> m_buffer;
    std::size_t m_readOffset = 0;
    std::size_t m_writeOffset = 0;
    std::size_t m_size = 0;
    runtime::AudioFormat m_format {};
    runtime::ClockSnapshot m_snapshot {};
    runtime::Generation m_generation = 1;
    std::chrono::microseconds m_playbackPosition { 0 };
    bool m_configured = false;
    bool m_closed = true;
};

inline CoreAudioRingBuffer::CoreAudioRingBuffer(std::size_t capacityBytes)
    : m_capacityBytes(capacityBytes == 0 ? 1 : capacityBytes)
    , m_buffer(m_capacityBytes)
{
}

inline void CoreAudioRingBuffer::configure(runtime::AudioFormat format, runtime::Generation generation)
{
    {
        std::scoped_lock lock(m_mutex);
        m_format = format;
        m_generation = generation;
        m_playbackPosition = std::chrono::microseconds { 0 };
        resetBufferLocked();
        m_configured = true;
        m_closed = false;
        m_snapshot = {
            .position = std::chrono::microseconds { 0 },
            .hardwareLatency = std::chrono::microseconds { 0 },
            .queuedDuration = std::chrono::microseconds { 0 },
            .generation = m_generation,
            .valid = true,
            .paused = false,
        };
    }
    m_notFull.notify_all();
}

inline bool CoreAudioRingBuffer::write(runtime::AudioBufferView buffer)
{
    std::unique_lock lock(m_mutex);
    if (!m_configured || m_closed || buffer.generation != m_generation)
        return false;
    const auto generation = buffer.generation;

    const auto frameBytes = bytesPerFrame();
    if (frameBytes == 0 || completeFrameBytes(m_capacityBytes) == 0)
        return false;

    if (buffer.bytes.empty())
        return true;

    if (buffer.bytes.size() % frameBytes != 0)
        return false;

    if (m_size == 0)
        m_playbackPosition = buffer.pts;

    std::size_t copied = 0;
    while (copied < buffer.bytes.size()) {
        m_notFull.wait(lock, [this, generation]()
        {
            return m_closed ||
                generation != m_generation ||
                completeFrameBytes(m_capacityBytes - m_size) > 0;
        });

        if (m_closed || generation != m_generation)
            return false;

        const auto available = completeFrameBytes(m_capacityBytes - m_size);
        const auto chunk = std::min(available, buffer.bytes.size() - copied);
        copyIntoRing(buffer.bytes.subspan(copied, chunk));
        copied += chunk;
    }

    updateSnapshotLocked();
    return true;
}

inline CoreAudioRingBufferReadResult CoreAudioRingBuffer::read(std::span<std::byte> destination)
{
    CoreAudioRingBufferReadResult result;
    {
        std::scoped_lock lock(m_mutex);
        result.copiedBytes = completeFrameBytes(std::min(destination.size(), m_size));
        result.silenceBytes = destination.size() - result.copiedBytes;

        copyFromRing(destination.first(result.copiedBytes));
        std::fill(destination.begin() + static_cast<std::ptrdiff_t>(result.copiedBytes),
                  destination.end(),
                  std::byte { 0 });

        m_playbackPosition += durationForBytes(result.copiedBytes);
        updateSnapshotLocked();
    }
    m_notFull.notify_all();
    return result;
}

inline void CoreAudioRingBuffer::flush()
{
    {
        std::scoped_lock lock(m_mutex);
        ++m_generation;
        resetBufferLocked();
        m_playbackPosition = std::chrono::microseconds { 0 };
        m_snapshot.position = std::chrono::microseconds { 0 };
        m_snapshot.queuedDuration = std::chrono::microseconds { 0 };
        m_snapshot.generation = m_generation;
        m_snapshot.valid = m_configured && !m_closed;
    }
    m_notFull.notify_all();
}

inline void CoreAudioRingBuffer::close()
{
    {
        std::scoped_lock lock(m_mutex);
        m_closed = true;
        resetBufferLocked();
        m_snapshot.valid = false;
        m_snapshot.queuedDuration = std::chrono::microseconds { 0 };
    }
    m_notFull.notify_all();
}

inline runtime::ClockSnapshot CoreAudioRingBuffer::clock() const
{
    std::scoped_lock lock(m_mutex);
    return m_snapshot;
}

inline std::size_t CoreAudioRingBuffer::bytesPerSample() const
{
    switch (m_format.sampleFormat) {
    case runtime::AudioSampleFormat::UInt8:
        return 1;
    case runtime::AudioSampleFormat::Int16:
        return 2;
    case runtime::AudioSampleFormat::Int32:
    case runtime::AudioSampleFormat::Float32:
        return 4;
    case runtime::AudioSampleFormat::Unknown:
    case runtime::AudioSampleFormat::Float32Planar:
        return 0;
    }

    return 0;
}

inline std::size_t CoreAudioRingBuffer::bytesPerFrame() const
{
    if (m_format.channels <= 0)
        return 0;

    return bytesPerSample() * static_cast<std::size_t>(m_format.channels);
}

inline std::size_t CoreAudioRingBuffer::completeFrameBytes(std::size_t bytes) const
{
    const auto frameBytes = bytesPerFrame();
    if (frameBytes == 0)
        return 0;

    return bytes - (bytes % frameBytes);
}

inline std::chrono::microseconds CoreAudioRingBuffer::durationForBytes(std::size_t bytes) const
{
    const auto frameBytes = bytesPerFrame();
    if (m_format.sampleRate <= 0 || frameBytes == 0)
        return std::chrono::microseconds { 0 };

    const auto frames = static_cast<std::int64_t>(bytes / frameBytes);

    return std::chrono::microseconds {
        frames * 1000000 / static_cast<std::int64_t>(m_format.sampleRate)
    };
}

inline void CoreAudioRingBuffer::copyIntoRing(std::span<const std::byte> source)
{
    std::size_t copied = 0;
    while (copied < source.size()) {
        const auto contiguous = std::min(source.size() - copied, m_capacityBytes - m_writeOffset);
        std::memcpy(m_buffer.data() + m_writeOffset,
                    source.data() + copied,
                    contiguous);
        copied += contiguous;
        m_writeOffset = (m_writeOffset + contiguous) % m_capacityBytes;
        m_size += contiguous;
    }
}

inline void CoreAudioRingBuffer::copyFromRing(std::span<std::byte> destination)
{
    std::size_t copied = 0;
    while (copied < destination.size()) {
        const auto contiguous = std::min(destination.size() - copied, m_capacityBytes - m_readOffset);
        std::memcpy(destination.data() + copied,
                    m_buffer.data() + m_readOffset,
                    contiguous);
        copied += contiguous;
        m_readOffset = (m_readOffset + contiguous) % m_capacityBytes;
        m_size -= contiguous;
    }
}

inline void CoreAudioRingBuffer::resetBufferLocked()
{
    m_readOffset = 0;
    m_writeOffset = 0;
    m_size = 0;
}

inline void CoreAudioRingBuffer::updateSnapshotLocked()
{
    m_snapshot.position = m_playbackPosition;
    m_snapshot.queuedDuration = durationForBytes(m_size);
    m_snapshot.generation = m_generation;
    m_snapshot.valid = m_configured && !m_closed;
}

} // namespace media_sdk::platform::macos
