#pragma once

#include "media_sdk/Result.h"
#include "media_sdk/runtime/AudioOutput.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

namespace media_sdk::platform::macos {

struct AudioRenderCallback {
    using Function = void (*)(void* context, std::span<std::byte> destination) noexcept;

    Function function = nullptr;
    void* context = nullptr;
};

struct AudioRenderDeviceConfig {
    runtime::AudioFormat format {};
    AudioRenderCallback callback {};
};

struct AudioRenderDeviceDiagnostics {
    std::uint64_t startFailures = 0;
    std::uint64_t stopFailures = 0;
    std::uint64_t resetFailures = 0;
};

class IAudioRenderDevice {
public:
    virtual ~IAudioRenderDevice() = default;

    [[nodiscard("open failures must leave the audio output closed")]]
    virtual Result<void> open(const AudioRenderDeviceConfig& config) = 0;
    [[nodiscard("start failures affect audio clock validity and playback state")]]
    virtual Result<void> start() = 0;
    virtual void stop() noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual void close() noexcept = 0;
    [[nodiscard("hardware latency is part of A/V sync diagnostics")]]
    virtual std::chrono::microseconds hardwareLatency() const noexcept = 0;
    [[nodiscard("diagnostics are used by platform-audio tests")]]
    virtual AudioRenderDeviceDiagnostics diagnostics() const noexcept = 0;
};

} // namespace media_sdk::platform::macos
