#include "CoreAudioRingBuffer.h"

#include <algorithm>
#include <cstring>

namespace media_sdk::platform::macos {

CoreAudioRingBuffer::CoreAudioRingBuffer(std::size_t capacityBytes)
    : m_capacityBytes(capacityBytes == 0 ? 1 : capacityBytes)
    , m_buffer(m_capacityBytes)
{
}

void CoreAudioRingBuffer::configure(runtime::AudioFormat format, runtime::Generation generation)
{
    {
        std::scoped_lock lock(m_writerMutex);
        beginControlUpdate();
        m_bytesPerFrame.store(bytesPerFrame(format), std::memory_order_release);
        m_sampleRate.store(format.sampleRate, std::memory_order_release);
        m_generation.store(generation, std::memory_order_release);
        m_playbackPositionUs.store(0, std::memory_order_release);
        m_clockAnchorPositionUs.store(0, std::memory_order_release);
        m_clockAnchorReadCursor.store(0, std::memory_order_release);
        m_playbackRateMillionths.store(runtime::kPlaybackRateScale, std::memory_order_release);
        m_rateAnchored.store(false, std::memory_order_release);
        resetCursors();
        m_configured.store(true, std::memory_order_release);
        m_closed.store(false, std::memory_order_release);
        endControlUpdate();
    }
    wakeAllWriters();
}

bool CoreAudioRingBuffer::write(runtime::AudioBufferView buffer)
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

    if (!runtime::isPlaybackRateSupported(buffer.playbackRate))
        return false;
    if (buffer.bytes.empty())
        return true;
    if (buffer.bytes.size() % frameBytes != 0)
        return false;

    const auto rateMillionths = runtime::playbackRateMillionths(buffer.playbackRate);
    if (m_rateAnchored.load(std::memory_order_acquire)) {
        if (rateMillionths != m_playbackRateMillionths.load(std::memory_order_acquire))
            return false;
    } else {
        m_playbackRateMillionths.store(rateMillionths, std::memory_order_release);
        m_rateAnchored.store(true, std::memory_order_release);
    }

    if (queuedBytes() == 0) {
        const auto readCursor = m_readCursor.load(std::memory_order_acquire);
        m_clockAnchorReadCursor.store(readCursor, std::memory_order_release);
        m_clockAnchorPositionUs.store(buffer.pts.count(), std::memory_order_release);
        m_playbackPositionUs.store(buffer.pts.count(), std::memory_order_release);
    }

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

CoreAudioRingBufferReadResult CoreAudioRingBuffer::read(std::span<std::byte> destination)
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
    const auto nextReadCursor = readCursor + result.copiedBytes;
    m_readCursor.store(nextReadCursor, std::memory_order_release);
    m_playbackPositionUs.store(mediaPositionForReadCursor(nextReadCursor),
                               std::memory_order_release);
    if (result.copiedBytes > 0)
        wakeOneWriter();
    return result;
}

void CoreAudioRingBuffer::flush()
{
    {
        std::scoped_lock lock(m_writerMutex);
        beginControlUpdate();
        m_generation.fetch_add(1, std::memory_order_acq_rel);
        resetCursors();
        m_playbackPositionUs.store(0, std::memory_order_release);
        m_clockAnchorPositionUs.store(0, std::memory_order_release);
        m_clockAnchorReadCursor.store(0, std::memory_order_release);
        m_playbackRateMillionths.store(runtime::kPlaybackRateScale, std::memory_order_release);
        m_rateAnchored.store(false, std::memory_order_release);
        endControlUpdate();
    }
    wakeAllWriters();
}

void CoreAudioRingBuffer::close()
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

runtime::ClockSnapshot CoreAudioRingBuffer::clock() const
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

std::size_t CoreAudioRingBuffer::bytesPerSample(runtime::AudioSampleFormat sampleFormat) const
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

std::size_t CoreAudioRingBuffer::bytesPerFrame(runtime::AudioFormat format) const
{
    if (format.channels <= 0)
        return 0;

    return bytesPerSample(format.sampleFormat) * static_cast<std::size_t>(format.channels);
}

std::size_t CoreAudioRingBuffer::completeFrameBytes(std::size_t bytes) const
{
    const auto frameBytes = m_bytesPerFrame.load(std::memory_order_acquire);
    if (frameBytes == 0)
        return 0;

    return bytes - (bytes % frameBytes);
}

std::size_t CoreAudioRingBuffer::queuedBytes() const
{
    const auto readCursor = m_readCursor.load(std::memory_order_acquire);
    const auto writeCursor = m_writeCursor.load(std::memory_order_acquire);
    if (writeCursor < readCursor)
        return 0;

    return static_cast<std::size_t>(
        std::min<std::uint64_t>(writeCursor - readCursor, m_capacityBytes));
}

std::chrono::microseconds CoreAudioRingBuffer::durationForBytes(std::size_t bytes) const
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

std::int64_t CoreAudioRingBuffer::mediaPositionForReadCursor(std::uint64_t readCursor) const
{
    if (!m_rateAnchored.load(std::memory_order_acquire))
        return m_playbackPositionUs.load(std::memory_order_acquire);

    const auto anchorCursor = m_clockAnchorReadCursor.load(std::memory_order_acquire);
    const auto anchorPosition = m_clockAnchorPositionUs.load(std::memory_order_acquire);
    const auto frameBytes = m_bytesPerFrame.load(std::memory_order_acquire);
    const auto sampleRate = m_sampleRate.load(std::memory_order_acquire);
    if (readCursor < anchorCursor || frameBytes == 0 || sampleRate <= 0)
        return anchorPosition;

    const auto frames = (readCursor - anchorCursor) / frameBytes;
    const auto rate = static_cast<std::uint64_t>(
        m_playbackRateMillionths.load(std::memory_order_acquire));
    const auto wholeSeconds = frames / static_cast<std::uint64_t>(sampleRate);
    const auto remainingFrames = frames % static_cast<std::uint64_t>(sampleRate);
    const auto mediaDurationUs = wholeSeconds * rate
        + remainingFrames * rate / static_cast<std::uint64_t>(sampleRate);
    return anchorPosition + static_cast<std::int64_t>(mediaDurationUs);
}

void CoreAudioRingBuffer::copyIntoRing(std::span<const std::byte> source,
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

void CoreAudioRingBuffer::copyFromRing(std::span<std::byte> destination,
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

void CoreAudioRingBuffer::beginControlUpdate()
{
    const auto epoch = m_epoch.load(std::memory_order_acquire);
    if (epoch % 2 == 0)
        m_epoch.store(epoch + 1, std::memory_order_release);
}

void CoreAudioRingBuffer::endControlUpdate()
{
    const auto epoch = m_epoch.load(std::memory_order_acquire);
    m_epoch.store((epoch % 2 == 0 ? epoch : epoch + 1), std::memory_order_release);
}

void CoreAudioRingBuffer::resetCursors()
{
    m_readCursor.store(0, std::memory_order_release);
    m_writeCursor.store(0, std::memory_order_release);
}

void CoreAudioRingBuffer::wakeOneWriter()
{
    m_wakeupSequence.fetch_add(1, std::memory_order_release);
    std::atomic_notify_one(&m_wakeupSequence);
}

void CoreAudioRingBuffer::wakeAllWriters()
{
    m_wakeupSequence.fetch_add(1, std::memory_order_release);
    std::atomic_notify_all(&m_wakeupSequence);
}

} // namespace media_sdk::platform::macos
