#pragma once

#include "AudioRenderDevice.h"

#include <memory>

namespace media_sdk::platform::macos {

[[nodiscard("CoreAudioAudioOutput requires a concrete macOS render device")]]
std::unique_ptr<IAudioRenderDevice> makeMacAudioUnitRenderDevice();

} // namespace media_sdk::platform::macos
