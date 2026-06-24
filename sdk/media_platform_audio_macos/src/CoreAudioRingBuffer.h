#pragma once

#include "media_sdk/runtime/AudioOutput.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <vector>

namespace media_sdk::platform::macos {

struct CoreAudioRingBufferReadResult {
    std::size_t copiedBytes = 0;
    std::size_t silenceBytes = 0;
};

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
    std::chrono::microseconds durationForBytes(std::size_t bytes) const;
    void updateSnapshotLocked();

    const std::size_t m_capacityBytes;
    mutable std::mutex m_mutex;
    std::condition_variable m_notFull;
    std::deque<std::byte> m_buffer;
    runtime::AudioFormat m_format {};
    runtime::ClockSnapshot m_snapshot {};
    runtime::Generation m_generation = 1;
    std::chrono::microseconds m_playbackPosition { 0 };
    bool m_configured = false;
    bool m_closed = true;
};

inline CoreAudioRingBuffer::CoreAudioRingBuffer(std::size_t capacityBytes)
    : m_capacityBytes(capacityBytes == 0 ? 1 : capacityBytes)
{
}

inline void CoreAudioRingBuffer::configure(runtime::AudioFormat format, runtime::Generation generation)
{
    {
        std::scoped_lock lock(m_mutex);
        m_format = format;
        m_generation = generation;
        m_playbackPosition = std::chrono::microseconds { 0 };
        m_buffer.clear();
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

    if (m_buffer.empty())
        m_playbackPosition = buffer.pts;

    std::size_t copied = 0;
    while (copied < buffer.bytes.size()) {
        m_notFull.wait(lock, [this]()
        {
            return m_closed || m_buffer.size() < m_capacityBytes;
        });

        if (m_closed)
            return false;

        const auto available = m_capacityBytes - m_buffer.size();
        const auto chunk = std::min(available, buffer.bytes.size() - copied);
        for (std::size_t i = 0; i < chunk; ++i)
            m_buffer.push_back(buffer.bytes[copied + i]);
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
        for (std::byte& sample : destination) {
            if (!m_buffer.empty()) {
                sample = m_buffer.front();
                m_buffer.pop_front();
                ++result.copiedBytes;
            } else {
                sample = std::byte { 0 };
                ++result.silenceBytes;
            }
        }

        m_playbackPosition += durationForBytes(destination.size());
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
        m_buffer.clear();
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
        m_buffer.clear();
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

inline std::chrono::microseconds CoreAudioRingBuffer::durationForBytes(std::size_t bytes) const
{
    if (m_format.sampleRate <= 0 || m_format.channels <= 0)
        return std::chrono::microseconds { 0 };

    constexpr int bytesPerSample = 4;
    const auto bytesPerSecond = static_cast<std::int64_t>(m_format.sampleRate)
        * static_cast<std::int64_t>(m_format.channels)
        * bytesPerSample;
    if (bytesPerSecond <= 0)
        return std::chrono::microseconds { 0 };

    return std::chrono::microseconds {
        static_cast<std::int64_t>(bytes) * 1000000 / bytesPerSecond
    };
}

inline void CoreAudioRingBuffer::updateSnapshotLocked()
{
    m_snapshot.position = m_playbackPosition;
    m_snapshot.queuedDuration = durationForBytes(m_buffer.size());
    m_snapshot.generation = m_generation;
    m_snapshot.valid = m_configured && !m_closed;
}

} // namespace media_sdk::platform::macos
