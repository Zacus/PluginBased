#include "MacAudioUnitRenderDevice.h"

#include "media_sdk/Error.h"

#include <algorithm>
#include <atomic>
#include <AudioUnit/AudioUnit.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace media_sdk::platform::macos {
namespace {

using namespace std::chrono_literals;

Result<void> stateError(const char* message, OSStatus status = noErr)
{
    return Result<void>::failure({
        .code = MediaErrorCode::InternalStateError,
        .message = message,
        .detail = status == noErr ? std::string {} : "OSStatus=" + std::to_string(status),
    });
}

Result<void> unsupportedFormat(const char* message, OSStatus status = noErr)
{
    return Result<void>::failure({
        .code = MediaErrorCode::UnsupportedFormat,
        .message = message,
        .detail = status == noErr ? std::string {} : "OSStatus=" + std::to_string(status),
    });
}

void fillSilence(AudioBufferList* buffers) noexcept
{
    if (!buffers)
        return;

    for (UInt32 index = 0; index < buffers->mNumberBuffers; ++index) {
        auto& buffer = buffers->mBuffers[index];
        if (buffer.mData && buffer.mDataByteSize > 0) {
            std::fill_n(static_cast<std::byte*>(buffer.mData),
                        static_cast<std::size_t>(buffer.mDataByteSize),
                        std::byte { 0 });
        }
    }
}

class MacAudioUnitRenderDevice final : public IAudioRenderDevice {
public:
    ~MacAudioUnitRenderDevice() override
    {
        close();
    }

    Result<void> open(const AudioRenderDeviceConfig& config) override
    {
        if (config.format.sampleRate <= 0 || config.format.channels <= 0)
            return stateError("MacAudioUnitRenderDevice requires a valid audio format");
        if (config.format.sampleFormat != runtime::AudioSampleFormat::Float32)
            return unsupportedFormat("MacAudioUnitRenderDevice requires interleaved float32 audio");
        if (!config.callback.function || !config.callback.context)
            return stateError("MacAudioUnitRenderDevice requires a render callback");

        close();

        AudioComponentDescription description {};
        description.componentType = kAudioUnitType_Output;
        description.componentSubType = kAudioUnitSubType_DefaultOutput;
        description.componentManufacturer = kAudioUnitManufacturer_Apple;

        AudioComponent component = AudioComponentFindNext(nullptr, &description);
        if (!component)
            return stateError("MacAudioUnitRenderDevice failed to find default output AudioUnit");

        AudioUnit newUnit = nullptr;
        OSStatus status = AudioComponentInstanceNew(component, &newUnit);
        if (status != noErr || !newUnit)
            return stateError("MacAudioUnitRenderDevice failed to create AudioUnit instance", status);

        m_unit = newUnit;
        m_callbackContext.store(config.callback.context, std::memory_order_release);
        m_callbackFunction.store(config.callback.function, std::memory_order_release);
        m_acceptingCallbacks.store(false, std::memory_order_release);

        AudioStreamBasicDescription streamDescription {};
        streamDescription.mSampleRate = config.format.sampleRate;
        streamDescription.mFormatID = kAudioFormatLinearPCM;
        streamDescription.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        streamDescription.mBytesPerPacket =
            static_cast<UInt32>(config.format.channels * sizeof(float));
        streamDescription.mFramesPerPacket = 1;
        streamDescription.mBytesPerFrame =
            static_cast<UInt32>(config.format.channels * sizeof(float));
        streamDescription.mChannelsPerFrame = static_cast<UInt32>(config.format.channels);
        streamDescription.mBitsPerChannel = 8 * sizeof(float);

        status = AudioUnitSetProperty(m_unit,
                                      kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Input,
                                      0,
                                      &streamDescription,
                                      sizeof(streamDescription));
        if (status != noErr) {
            close();
            return unsupportedFormat(
                "MacAudioUnitRenderDevice failed to set AudioUnit stream format",
                status);
        }

        AURenderCallbackStruct callback {
            .inputProc = &MacAudioUnitRenderDevice::renderCallback,
            .inputProcRefCon = this,
        };
        status = AudioUnitSetProperty(m_unit,
                                      kAudioUnitProperty_SetRenderCallback,
                                      kAudioUnitScope_Input,
                                      0,
                                      &callback,
                                      sizeof(callback));
        if (status != noErr) {
            close();
            return stateError(
                "MacAudioUnitRenderDevice failed to set AudioUnit render callback",
                status);
        }

        status = AudioUnitInitialize(m_unit);
        if (status != noErr) {
            close();
            return stateError("MacAudioUnitRenderDevice failed to initialize AudioUnit", status);
        }
        m_initialized = true;

        updateHardwareLatency();
        return Result<void>::success();
    }

    Result<void> start() override
    {
        if (!m_unit || !m_initialized)
            return stateError("MacAudioUnitRenderDevice is not open");
        if (m_running)
            return Result<void>::success();

        m_acceptingCallbacks.store(true, std::memory_order_release);
        const OSStatus status = AudioOutputUnitStart(m_unit);
        if (status != noErr) {
            m_acceptingCallbacks.store(false, std::memory_order_release);
            ++m_diagnostics.startFailures;
            return stateError("MacAudioUnitRenderDevice failed to start AudioUnit", status);
        }

        m_running = true;
        return Result<void>::success();
    }

    void stop() noexcept override
    {
        if (!m_unit || !m_running)
            return;

        m_acceptingCallbacks.store(false, std::memory_order_release);
        const OSStatus status = AudioOutputUnitStop(m_unit);
        if (status != noErr)
            ++m_diagnostics.stopFailures;
        m_running = false;
    }

    void reset() noexcept override
    {
        if (!m_unit)
            return;

        const OSStatus status = AudioUnitReset(m_unit, kAudioUnitScope_Global, 0);
        if (status != noErr)
            ++m_diagnostics.resetFailures;
    }

    void close() noexcept override
    {
        m_acceptingCallbacks.store(false, std::memory_order_release);
        if (!m_unit) {
            m_callbackFunction.store(nullptr, std::memory_order_release);
            m_callbackContext.store(nullptr, std::memory_order_release);
            m_hardwareLatency = 0us;
            return;
        }

        if (m_running)
            AudioOutputUnitStop(m_unit);
        m_running = false;

        if (m_initialized)
            AudioUnitUninitialize(m_unit);
        m_initialized = false;

        AudioComponentInstanceDispose(m_unit);
        m_unit = nullptr;
        m_callbackFunction.store(nullptr, std::memory_order_release);
        m_callbackContext.store(nullptr, std::memory_order_release);
        m_hardwareLatency = 0us;
    }

    std::chrono::microseconds hardwareLatency() const noexcept override
    {
        return m_hardwareLatency;
    }

    AudioRenderDeviceDiagnostics diagnostics() const noexcept override
    {
        return m_diagnostics;
    }

private:
    static OSStatus renderCallback(void* userData,
                                   AudioUnitRenderActionFlags*,
                                   const AudioTimeStamp*,
                                   UInt32,
                                   UInt32,
                                   AudioBufferList* ioData)
    {
        auto* device = static_cast<MacAudioUnitRenderDevice*>(userData);
        const auto callbackFunction = device
            ? device->m_callbackFunction.load(std::memory_order_acquire)
            : nullptr;
        void* const callbackContext = device
            ? device->m_callbackContext.load(std::memory_order_acquire)
            : nullptr;
        if (!device || !ioData ||
            ioData->mNumberBuffers != 1 ||
            !ioData->mBuffers[0].mData ||
            ioData->mBuffers[0].mDataByteSize == 0 ||
            !device->m_acceptingCallbacks.load(std::memory_order_acquire) ||
            !callbackFunction ||
            !callbackContext) {
            fillSilence(ioData);
            return noErr;
        }

        auto& buffer = ioData->mBuffers[0];
        std::span<std::byte> destination {
            static_cast<std::byte*>(buffer.mData),
            static_cast<std::size_t>(buffer.mDataByteSize),
        };
        callbackFunction(callbackContext, destination);
        return noErr;
    }

    void updateHardwareLatency() noexcept
    {
        if (!m_unit)
            return;

        Float64 latencySeconds = 0.0;
        UInt32 latencySize = sizeof(latencySeconds);
        const OSStatus status = AudioUnitGetProperty(m_unit,
                                                     kAudioUnitProperty_Latency,
                                                     kAudioUnitScope_Global,
                                                     0,
                                                     &latencySeconds,
                                                     &latencySize);
        if (status == noErr && latencySeconds > 0.0) {
            m_hardwareLatency = std::chrono::microseconds {
                static_cast<std::int64_t>(latencySeconds * 1000000.0)
            };
        } else {
            m_hardwareLatency = 0us;
        }
    }

    AudioUnit m_unit = nullptr;
    AudioRenderDeviceDiagnostics m_diagnostics {};
    std::chrono::microseconds m_hardwareLatency { 0 };
    std::atomic<AudioRenderCallback::Function> m_callbackFunction { nullptr };
    std::atomic<void*> m_callbackContext { nullptr };
    std::atomic_bool m_acceptingCallbacks { false };
    bool m_initialized = false;
    bool m_running = false;
};

} // namespace

std::unique_ptr<IAudioRenderDevice> makeMacAudioUnitRenderDevice()
{
    return std::make_unique<MacAudioUnitRenderDevice>();
}

} // namespace media_sdk::platform::macos
