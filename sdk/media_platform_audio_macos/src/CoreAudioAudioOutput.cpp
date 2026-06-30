#include "media_sdk/platform/macos/CoreAudioAudioOutput.h"

#include "CoreAudioOutputEngine.h"
#include "MacAudioUnitRenderDevice.h"

namespace media_sdk::platform::macos {

struct CoreAudioAudioOutput::Impl {
    CoreAudioOutputEngine engine { makeMacAudioUnitRenderDevice() };
};

CoreAudioAudioOutput::CoreAudioAudioOutput()
    : m_impl(std::make_unique<Impl>())
{
}

CoreAudioAudioOutput::~CoreAudioAudioOutput() = default;

Result<void> CoreAudioAudioOutput::open(const runtime::AudioFormat& format)
{
    return m_impl->engine.open(format);
}

Result<void> CoreAudioAudioOutput::write(runtime::AudioBufferView buffer)
{
    return m_impl->engine.write(buffer);
}

runtime::ClockSnapshot CoreAudioAudioOutput::clock() const
{
    return m_impl->engine.clock();
}

void CoreAudioAudioOutput::pause()
{
    m_impl->engine.pause();
}

void CoreAudioAudioOutput::resume()
{
    m_impl->engine.resume();
}

void CoreAudioAudioOutput::flush()
{
    m_impl->engine.flush();
}

void CoreAudioAudioOutput::close()
{
    m_impl->engine.close();
}

} // namespace media_sdk::platform::macos
