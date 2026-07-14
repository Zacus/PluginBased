#pragma once

#include "media_sdk/runtime/AudioOutput.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
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
    std::int64_t mediaPositionForReadCursor(std::uint64_t readCursor) const;
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
    std::atomic<std::int64_t> m_clockAnchorPositionUs { 0 };
    std::atomic<std::uint64_t> m_clockAnchorReadCursor { 0 };
    std::atomic<std::uint32_t> m_playbackRateMillionths { runtime::kPlaybackRateScale };
    std::atomic_bool m_rateAnchored { false };
    std::atomic_bool m_configured { false };
    std::atomic_bool m_closed { true };
};

} // namespace media_sdk::platform::macos
