#include "CoreAudioOutputEngine.h"

#include "media_sdk/Error.h"

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

} // namespace

CoreAudioOutputEngine::CoreAudioOutputEngine(std::unique_ptr<IAudioRenderDevice> device)
    : m_device(std::move(device))
    , m_ringBuffer(48000 * 2 * 4)
{
}

CoreAudioOutputEngine::~CoreAudioOutputEngine() = default;

Result<void> CoreAudioOutputEngine::open(const runtime::AudioFormat&)
{
    return stateError("CoreAudioOutputEngine is not implemented");
}

Result<void> CoreAudioOutputEngine::write(runtime::AudioBufferView)
{
    return stateError("CoreAudioOutputEngine is not open");
}

runtime::ClockSnapshot CoreAudioOutputEngine::clock() const
{
    return {};
}

void CoreAudioOutputEngine::pause()
{
}

void CoreAudioOutputEngine::resume()
{
}

void CoreAudioOutputEngine::flush()
{
}

void CoreAudioOutputEngine::close()
{
}

void CoreAudioOutputEngine::renderCallback(void* context, std::span<std::byte> destination) noexcept
{
    if (!context)
        return;

    static_cast<CoreAudioOutputEngine*>(context)->render(destination);
}

void CoreAudioOutputEngine::render(std::span<std::byte>) noexcept
{
}

} // namespace media_sdk::platform::macos
