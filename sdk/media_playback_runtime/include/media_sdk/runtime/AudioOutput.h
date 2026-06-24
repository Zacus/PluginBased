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

    virtual Result<void> open(const AudioFormat& format) = 0;
    virtual Result<void> write(AudioBufferView buffer) = 0;
    virtual ClockSnapshot clock() const = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void flush() = 0;
    virtual void close() = 0;
};

} // namespace media_sdk::runtime
