#pragma once

#include "media_sdk/Result.h"
#include "media_sdk/runtime/RuntimeTypes.h"

#include <chrono>
#include <cstddef>
#include <span>

namespace media_sdk::runtime {

enum class AudioSampleFormat {
    Unknown,
    UInt8,
    Int16,
    Int32,
    Float32,
    Float32Planar
};

struct AudioFormat {
    int sampleRate = 0;
    int channels = 0;
    AudioSampleFormat sampleFormat = AudioSampleFormat::Unknown;
};

struct AudioBufferView {
    std::span<const std::byte> bytes;
    std::chrono::microseconds pts { 0 };
    Generation generation = 0;
};

struct ClockSnapshot {
    std::chrono::microseconds position { 0 };
    std::chrono::microseconds hardwareLatency { 0 };
    std::chrono::microseconds queuedDuration { 0 };
    Generation generation = 0;
    bool valid = false;
    bool paused = false;
};

class IAudioOutput {
public:
    virtual ~IAudioOutput() = default;

    [[nodiscard("inspect the audio output open result before writing audio")]]
    virtual Result<void> open(const AudioFormat& format) = 0;
    [[nodiscard("inspect the audio write result because stale generations or closed outputs reject frames")]]
    virtual Result<void> write(AudioBufferView buffer) = 0;
    [[nodiscard("clock snapshots drive A/V sync decisions")]]
    virtual ClockSnapshot clock() const = 0;
    virtual void pause() = 0;
    [[nodiscard("resume can fail when the native audio device cannot start")]]
    virtual Result<void> resume() = 0;
    [[nodiscard("flush can fail when the native audio device cannot restart after reset")]]
    virtual Result<void> flush() = 0;
    virtual void close() = 0;
};

} // namespace media_sdk::runtime
