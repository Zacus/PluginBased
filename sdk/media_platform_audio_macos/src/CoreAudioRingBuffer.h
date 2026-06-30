#pragma once

#include "media_sdk/runtime/AudioOutput.h"

#include <algorithm>
#include <atomic>
#include <chrono>
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

// SPSC PCM ring buffer.
// The producer/runtime thread may block in write() for capacity. The CoreAudio
// callback path calls read(); read() is lock-free and returns silence on underrun.
// Stored data is interleaved PCM, and queue operations publish only complete PCM frames.
class CoreAudioRingBuffer
{
public:
    explicit CoreAudioRingBuffer(std::size_t capacityBytes);

    void configure(runtime::AudioFormat format, runtime::Generation generation);
    [[nodiscard("false means the audio frame was rejected or belongs to a stale generation")]]
    bool write(runtime::AudioBufferView buffer);
    [[nodiscard("read result reports copied audio vs generated silence for diagnostics")]]
    CoreAudioRingBufferReadResult read(std::span<std::byte> destination);
    void flush();
    void close();

    [[nodiscard("ring buffer clock snapshots drive runtime A/V sync")]]
    runtime::ClockSnapshot clock() const;

private:
    std::size_t bytesPerSample(runtime::AudioSampleFormat sampleFormat) const;
    std::size_t bytesPerFrame(runtime::AudioFormat format) const;
    std::size_t completeFrameBytes(std::size_t bytes) const;
    std::size_t queuedBytes() const;
    std::chrono::microseconds durationForBytes(std::size_t bytes) const;
    void copyIntoRing(std::span<const std::byte> source, std::uint64_t writeCursor);
    void copyFromRing(std::span<std::byte> destination, std::uint64_t readCursor) const;
    void wakeOneWriter();
    void wakeAllWriters();
    void beginControlUpdate();
    void endControlUpdate();
    void resetCursors();

    const std::size_t m_capacityBytes;
    mutable std::mutex m_writerMutex;
    std::vector<std::byte> m_buffer;
    std::atomic<std::uint64_t> m_readCursor { 0 };
    std::atomic<std::uint64_t> m_writeCursor { 0 };
    std::atomic<std::uint64_t> m_epoch { 1 };
    std::atomic<std::uint64_t> m_wakeupSequence { 0 };
    std::atomic<std::size_t> m_bytesPerFrame { 0 };
    std::atomic<int> m_sampleRate { 0 };
    std::atomic<runtime::Generation> m_generation { 1 };
    std::atomic<std::int64_t> m_playbackPositionUs { 0 };
    std::atomic_bool m_configured { false };
    std::atomic_bool m_closed { true };
};

inline CoreAudioRingBuffer::CoreAudioRingBuffer(std::size_t capacityBytes)
    : m_capacityBytes(capacityBytes == 0 ? 1 : capacityBytes)
    , m_buffer(m_capacityBytes)
{
}

inline void CoreAudioRingBuffer::configure(runtime::AudioFormat format, runtime::Generation generation)
{
    {
        std::scoped_lock lock(m_writerMutex);
        beginControlUpdate();
        m_bytesPerFrame.store(bytesPerFrame(format), std::memory_order_release);
        m_sampleRate.store(format.sampleRate, std::memory_order_release);
        m_generation.store(generation, std::memory_order_release);
        m_playbackPositionUs.store(0, std::memory_order_release);
        resetCursors();
        m_configured.store(true, std::memory_order_release);
        m_closed.store(false, std::memory_order_release);
        endControlUpdate();
    }
    wakeAllWriters();
}

inline bool CoreAudioRingBuffer::write(runtime::AudioBufferView buffer)
{
    std::unique_lock lock(m_writerMutex);
    if (!m_configured.load(std::memory_order_acquire) ||
        m_closed.load(std::memory_order_acquire) ||
        buffer.generation != m_generation.load(std::memory_order_acquire))
        return false;
    const auto generation = buffer.generation;
    const auto epoch = m_epoch.load(std::memory_order_acquire);

    const auto frameBytes = m_bytesPerFrame.load(std::memory_order_acquire);
    if (frameBytes == 0 || completeFrameBytes(m_capacityBytes) == 0)
        return false;

    if (buffer.bytes.empty())
        return true;

    if (buffer.bytes.size() % frameBytes != 0)
        return false;

    if (queuedBytes() == 0)
        m_playbackPositionUs.store(buffer.pts.count(), std::memory_order_release);

    std::size_t copied = 0;
    while (copied < buffer.bytes.size()) {
        std::size_t available = 0;
        while (true) {
            if (m_closed.load(std::memory_order_acquire) ||
                epoch != m_epoch.load(std::memory_order_acquire) ||
                generation != m_generation.load(std::memory_order_acquire))
                return false;

            available = completeFrameBytes(m_capacityBytes - queuedBytes());
            if (available > 0)
                break;

            const auto wakeupSequence = m_wakeupSequence.load(std::memory_order_acquire);
            lock.unlock();
            std::atomic_wait_explicit(&m_wakeupSequence,
                                      wakeupSequence,
                                      std::memory_order_acquire);
            lock.lock();
        }

        if (available == 0)
            continue;

        const auto chunk = std::min(available, buffer.bytes.size() - copied);
        const auto writeCursor = m_writeCursor.load(std::memory_order_relaxed);
        copyIntoRing(buffer.bytes.subspan(copied, chunk), writeCursor);
        if (m_closed.load(std::memory_order_acquire) ||
            epoch != m_epoch.load(std::memory_order_acquire) ||
            generation != m_generation.load(std::memory_order_acquire))
            return false;
        m_writeCursor.store(writeCursor + chunk, std::memory_order_release);
        copied += chunk;
    }

    return true;
}

inline CoreAudioRingBufferReadResult CoreAudioRingBuffer::read(std::span<std::byte> destination)
{
    CoreAudioRingBufferReadResult result;
    if (destination.empty())
        return result;

    const auto generation = m_generation.load(std::memory_order_acquire);
    const auto epoch = m_epoch.load(std::memory_order_acquire);
    if (epoch % 2 != 0 ||
        !m_configured.load(std::memory_order_acquire) ||
        m_closed.load(std::memory_order_acquire) ||
        m_bytesPerFrame.load(std::memory_order_acquire) == 0) {
        std::fill(destination.begin(), destination.end(), std::byte { 0 });
        result.silenceBytes = destination.size();
        return result;
    }

    const auto readCursor = m_readCursor.load(std::memory_order_acquire);
    const auto writeCursor = m_writeCursor.load(std::memory_order_acquire);
    const auto available = writeCursor >= readCursor
        ? std::min<std::uint64_t>(writeCursor - readCursor, m_capacityBytes)
        : 0;
    result.copiedBytes = completeFrameBytes(
        std::min<std::size_t>(destination.size(), static_cast<std::size_t>(available)));
    result.silenceBytes = destination.size() - result.copiedBytes;

    copyFromRing(destination.first(result.copiedBytes), readCursor);
    if (epoch != m_epoch.load(std::memory_order_acquire) ||
        generation != m_generation.load(std::memory_order_acquire) ||
        m_closed.load(std::memory_order_acquire)) {
        std::fill(destination.begin(), destination.end(), std::byte { 0 });
        return { .copiedBytes = 0, .silenceBytes = destination.size() };
    }

    std::fill(destination.begin() + static_cast<std::ptrdiff_t>(result.copiedBytes),
              destination.end(),
              std::byte { 0 });
    m_readCursor.store(readCursor + result.copiedBytes, std::memory_order_release);
    m_playbackPositionUs.fetch_add(durationForBytes(result.copiedBytes).count(),
                                   std::memory_order_acq_rel);
    if (result.copiedBytes > 0)
        wakeOneWriter();
    return result;
}

inline void CoreAudioRingBuffer::flush()
{
    {
        std::scoped_lock lock(m_writerMutex);
        beginControlUpdate();
        m_generation.fetch_add(1, std::memory_order_acq_rel);
        resetCursors();
        m_playbackPositionUs.store(0, std::memory_order_release);
        endControlUpdate();
    }
    wakeAllWriters();
}

inline void CoreAudioRingBuffer::close()
{
    {
        std::scoped_lock lock(m_writerMutex);
        beginControlUpdate();
        m_closed.store(true, std::memory_order_release);
        resetCursors();
        endControlUpdate();
    }
    wakeAllWriters();
}

inline runtime::ClockSnapshot CoreAudioRingBuffer::clock() const
{
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto epoch = m_epoch.load(std::memory_order_acquire);
        if (epoch % 2 != 0)
            continue;

        const bool configured = m_configured.load(std::memory_order_acquire);
        const bool closed = m_closed.load(std::memory_order_acquire);
        const auto position = std::chrono::microseconds {
            m_playbackPositionUs.load(std::memory_order_acquire) };
        const auto queuedDuration = durationForBytes(queuedBytes());
        const auto generation = m_generation.load(std::memory_order_acquire);

        if (epoch != m_epoch.load(std::memory_order_acquire))
            continue;

        return {
            .position = position,
            .hardwareLatency = std::chrono::microseconds { 0 },
            .queuedDuration = queuedDuration,
            .generation = generation,
            .valid = configured && !closed,
            .paused = false,
        };
    }

    return {
        .position = std::chrono::microseconds { 0 },
        .hardwareLatency = std::chrono::microseconds { 0 },
        .queuedDuration = std::chrono::microseconds { 0 },
        .generation = m_generation.load(std::memory_order_acquire),
        .valid = false,
        .paused = false,
    };
}

inline std::size_t CoreAudioRingBuffer::bytesPerSample(runtime::AudioSampleFormat sampleFormat) const
{
    switch (sampleFormat) {
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

inline std::size_t CoreAudioRingBuffer::bytesPerFrame(runtime::AudioFormat format) const
{
    if (format.channels <= 0)
        return 0;

    return bytesPerSample(format.sampleFormat) * static_cast<std::size_t>(format.channels);
}

inline std::size_t CoreAudioRingBuffer::completeFrameBytes(std::size_t bytes) const
{
    const auto frameBytes = m_bytesPerFrame.load(std::memory_order_acquire);
    if (frameBytes == 0)
        return 0;

    return bytes - (bytes % frameBytes);
}

inline std::size_t CoreAudioRingBuffer::queuedBytes() const
{
    const auto readCursor = m_readCursor.load(std::memory_order_acquire);
    const auto writeCursor = m_writeCursor.load(std::memory_order_acquire);
    if (writeCursor < readCursor)
        return 0;

    return static_cast<std::size_t>(
        std::min<std::uint64_t>(writeCursor - readCursor, m_capacityBytes));
}

inline std::chrono::microseconds CoreAudioRingBuffer::durationForBytes(std::size_t bytes) const
{
    const auto frameBytes = m_bytesPerFrame.load(std::memory_order_acquire);
    const int sampleRate = m_sampleRate.load(std::memory_order_acquire);
    if (sampleRate <= 0 || frameBytes == 0)
        return std::chrono::microseconds { 0 };

    const auto frames = static_cast<std::int64_t>(bytes / frameBytes);

    return std::chrono::microseconds {
        frames * 1000000 / static_cast<std::int64_t>(sampleRate)
    };
}

inline void CoreAudioRingBuffer::copyIntoRing(std::span<const std::byte> source,
                                              std::uint64_t writeCursor)
{
    std::size_t copied = 0;
    std::size_t writeOffset = static_cast<std::size_t>(writeCursor % m_capacityBytes);
    while (copied < source.size()) {
        const auto contiguous = std::min(source.size() - copied, m_capacityBytes - writeOffset);
        std::memcpy(m_buffer.data() + writeOffset,
                    source.data() + copied,
                    contiguous);
        copied += contiguous;
        writeOffset = (writeOffset + contiguous) % m_capacityBytes;
    }
}

inline void CoreAudioRingBuffer::copyFromRing(std::span<std::byte> destination,
                                              std::uint64_t readCursor) const
{
    std::size_t copied = 0;
    std::size_t readOffset = static_cast<std::size_t>(readCursor % m_capacityBytes);
    while (copied < destination.size()) {
        const auto contiguous = std::min(destination.size() - copied, m_capacityBytes - readOffset);
        std::memcpy(destination.data() + copied,
                    m_buffer.data() + readOffset,
                    contiguous);
        copied += contiguous;
        readOffset = (readOffset + contiguous) % m_capacityBytes;
    }
}

inline void CoreAudioRingBuffer::beginControlUpdate()
{
    const auto epoch = m_epoch.load(std::memory_order_acquire);
    if (epoch % 2 == 0)
        m_epoch.store(epoch + 1, std::memory_order_release);
}

inline void CoreAudioRingBuffer::endControlUpdate()
{
    const auto epoch = m_epoch.load(std::memory_order_acquire);
    m_epoch.store((epoch % 2 == 0 ? epoch : epoch + 1), std::memory_order_release);
}

inline void CoreAudioRingBuffer::resetCursors()
{
    m_readCursor.store(0, std::memory_order_release);
    m_writeCursor.store(0, std::memory_order_release);
}

inline void CoreAudioRingBuffer::wakeOneWriter()
{
    m_wakeupSequence.fetch_add(1, std::memory_order_release);
    std::atomic_notify_one(&m_wakeupSequence);
}

inline void CoreAudioRingBuffer::wakeAllWriters()
{
    m_wakeupSequence.fetch_add(1, std::memory_order_release);
    std::atomic_notify_all(&m_wakeupSequence);
}

} // namespace media_sdk::platform::macos
