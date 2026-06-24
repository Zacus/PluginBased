#include "media_sdk/platform/macos/CoreAudioAudioOutput.h"

#include "media_sdk/Error.h"

#include <algorithm>
#include <chrono>
#include <mutex>

namespace media_sdk::platform::macos {
namespace {

Result<void> stateError(const char* message)
{
    return Result<void>::failure({
        .code = MediaErrorCode::InternalStateError,
        .message = message,
        .detail = {},
    });
}

std::chrono::microseconds durationForBytes(
    std::size_t bytes,
    const runtime::AudioFormat& format)
{
    if (format.sampleRate <= 0 || format.channels <= 0)
        return std::chrono::microseconds { 0 };

    constexpr int bytesPerSample = 4;
    const auto bytesPerSecond = static_cast<std::int64_t>(format.sampleRate)
        * static_cast<std::int64_t>(format.channels)
        * bytesPerSample;
    if (bytesPerSecond <= 0)
        return std::chrono::microseconds { 0 };

    return std::chrono::microseconds {
        static_cast<std::int64_t>(bytes) * 1000000 / bytesPerSecond
    };
}

} // namespace

struct CoreAudioAudioOutput::Impl {
    mutable std::mutex mutex;
    runtime::AudioFormat format {};
    runtime::ClockSnapshot snapshot {};
    std::chrono::microseconds anchorPts { 0 };
    std::chrono::microseconds queuedDuration { 0 };
    std::uint64_t generation = 1;
    bool open = false;
    bool paused = false;
};

CoreAudioAudioOutput::CoreAudioAudioOutput()
    : m_impl(std::make_unique<Impl>())
{
}

CoreAudioAudioOutput::~CoreAudioAudioOutput() = default;

Result<void> CoreAudioAudioOutput::open(const runtime::AudioFormat& format)
{
    if (format.sampleRate <= 0 || format.channels <= 0)
        return stateError("CoreAudioAudioOutput requires a valid audio format");

    std::scoped_lock lock(m_impl->mutex);
    m_impl->format = format;
    m_impl->anchorPts = std::chrono::microseconds { 0 };
    m_impl->queuedDuration = std::chrono::microseconds { 0 };
    m_impl->snapshot = {
        .position = std::chrono::microseconds { 0 },
        .hardwareLatency = std::chrono::microseconds { 0 },
        .queuedDuration = std::chrono::microseconds { 0 },
        .generation = m_impl->generation,
        .valid = true,
        .paused = false,
    };
    m_impl->open = true;
    m_impl->paused = false;
    return Result<void>::success();
}

Result<void> CoreAudioAudioOutput::write(runtime::AudioBufferView buffer)
{
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->open)
        return stateError("CoreAudioAudioOutput is not open");

    if (buffer.generation != m_impl->generation)
        return stateError("CoreAudioAudioOutput rejected stale generation audio");

    if (m_impl->queuedDuration == std::chrono::microseconds { 0 })
        m_impl->anchorPts = buffer.pts;

    m_impl->queuedDuration += durationForBytes(buffer.bytes.size(), m_impl->format);
    m_impl->snapshot.position = m_impl->anchorPts;
    m_impl->snapshot.queuedDuration = m_impl->queuedDuration;
    m_impl->snapshot.generation = m_impl->generation;
    m_impl->snapshot.valid = true;
    m_impl->snapshot.paused = m_impl->paused;
    return Result<void>::success();
}

runtime::ClockSnapshot CoreAudioAudioOutput::clock() const
{
    std::scoped_lock lock(m_impl->mutex);
    return m_impl->snapshot;
}

void CoreAudioAudioOutput::pause()
{
    std::scoped_lock lock(m_impl->mutex);
    m_impl->paused = true;
    m_impl->snapshot.paused = true;
}

void CoreAudioAudioOutput::resume()
{
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->open)
        return;

    m_impl->paused = false;
    m_impl->snapshot.paused = false;
}

void CoreAudioAudioOutput::flush()
{
    std::scoped_lock lock(m_impl->mutex);
    ++m_impl->generation;
    m_impl->queuedDuration = std::chrono::microseconds { 0 };
    m_impl->anchorPts = std::chrono::microseconds { 0 };
    m_impl->snapshot.position = std::chrono::microseconds { 0 };
    m_impl->snapshot.queuedDuration = std::chrono::microseconds { 0 };
    m_impl->snapshot.generation = m_impl->generation;
    m_impl->snapshot.valid = m_impl->open;
    m_impl->snapshot.paused = m_impl->paused;
}

void CoreAudioAudioOutput::close()
{
    std::scoped_lock lock(m_impl->mutex);
    m_impl->open = false;
    m_impl->paused = false;
    m_impl->queuedDuration = std::chrono::microseconds { 0 };
    m_impl->snapshot.valid = false;
    m_impl->snapshot.paused = false;
    m_impl->snapshot.queuedDuration = std::chrono::microseconds { 0 };
}

} // namespace media_sdk::platform::macos
