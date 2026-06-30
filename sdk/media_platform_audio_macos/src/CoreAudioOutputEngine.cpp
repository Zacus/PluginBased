#include "CoreAudioOutputEngine.h"

#include "media_sdk/Error.h"

#include <chrono>

namespace media_sdk::platform::macos {
namespace {

using namespace std::chrono_literals;

Result<void> stateError(const char* message)
{
    return Result<void>::failure({
        .code = MediaErrorCode::InternalStateError,
        .message = message,
        .detail = {},
    });
}

Result<void> unsupportedFormat(const char* message)
{
    return Result<void>::failure({
        .code = MediaErrorCode::UnsupportedFormat,
        .message = message,
        .detail = {},
    });
}

} // namespace

CoreAudioOutputEngine::CoreAudioOutputEngine(std::unique_ptr<IAudioRenderDevice> device)
    : m_device(std::move(device))
    , m_ringBuffer(48000 * 2 * 4)
{
}

CoreAudioOutputEngine::~CoreAudioOutputEngine() = default;

Result<void> CoreAudioOutputEngine::open(const runtime::AudioFormat& format)
{
    if (format.sampleRate <= 0 || format.channels <= 0)
        return stateError("CoreAudioOutputEngine requires a valid audio format");
    if (format.sampleFormat != runtime::AudioSampleFormat::Float32)
        return unsupportedFormat("CoreAudioOutputEngine currently requires interleaved float32 audio");

    close();

    std::scoped_lock lock(m_mutex);
    if (!m_device)
        return stateError("CoreAudioOutputEngine requires an audio render device");

    m_format = format;
    m_generation = 1;
    m_ringBuffer.configure(format, m_generation);

    auto result = m_device->open({
        .format = format,
        .callback = {
            .function = &CoreAudioOutputEngine::renderCallback,
            .context = this,
        },
    });
    if (!result.ok()) {
        m_ringBuffer.close();
        m_format = {};
        m_open = false;
        m_paused = false;
        m_running = false;
        return result;
    }

    m_open = true;
    m_paused = false;
    m_running = false;
    return Result<void>::success();
}

Result<void> CoreAudioOutputEngine::write(runtime::AudioBufferView buffer)
{
    {
        std::scoped_lock lock(m_mutex);
        if (!m_open)
            return stateError("CoreAudioOutputEngine is not open");
    }

    if (!m_ringBuffer.write(buffer))
        return stateError("CoreAudioOutputEngine rejected audio buffer");

    return Result<void>::success();
}

runtime::ClockSnapshot CoreAudioOutputEngine::clock() const
{
    std::scoped_lock lock(m_mutex);
    auto snapshot = m_ringBuffer.clock();
    snapshot.paused = m_paused;
    snapshot.hardwareLatency = m_device ? m_device->hardwareLatency() : 0us;
    if (!m_open)
        snapshot.valid = false;
    return snapshot;
}

void CoreAudioOutputEngine::pause()
{
    std::scoped_lock lock(m_mutex);
    if (!m_open || !m_running)
        return;

    m_device->stop();
    m_running = false;
    m_paused = true;
}

void CoreAudioOutputEngine::resume()
{
    std::scoped_lock lock(m_mutex);
    if (!m_open || m_running || !m_device)
        return;

    auto result = m_device->start();
    if (result.ok()) {
        m_running = true;
        m_paused = false;
    }
}

void CoreAudioOutputEngine::flush()
{
    std::scoped_lock lock(m_mutex);
    if (!m_open)
        return;

    ++m_generation;
    m_ringBuffer.flush();
    if (m_device)
        m_device->reset();
}

void CoreAudioOutputEngine::close()
{
    std::scoped_lock lock(m_mutex);
    if (m_device) {
        if (m_running)
            m_device->stop();
        m_device->close();
    }
    m_ringBuffer.close();
    m_format = {};
    m_open = false;
    m_paused = false;
    m_running = false;
}

void CoreAudioOutputEngine::renderCallback(void* context, std::span<std::byte> destination) noexcept
{
    if (!context)
        return;

    static_cast<CoreAudioOutputEngine*>(context)->render(destination);
}

void CoreAudioOutputEngine::render(std::span<std::byte> destination) noexcept
{
    (void)m_ringBuffer.read(destination);
}

} // namespace media_sdk::platform::macos
