#pragma once

#include "media_sdk/Result.h"
#include "media_sdk/runtime/AudioOutput.h"

#include <chrono>
#include <cstddef>
#include <vector>

namespace media_sdk::runtime {

struct AudioTempoBuffer {
    std::vector<std::byte> bytes;
    std::chrono::microseconds pts { 0 };
};

struct AudioTempoOutput {
    std::vector<AudioTempoBuffer> buffers;
};

// Called exclusively by the runtime audio thread. reset() discards buffered
// samples; configure() must be called before processing the next timeline.
class IAudioTempoProcessor
{
public:
    virtual ~IAudioTempoProcessor() = default;

    [[nodiscard("tempo configuration can reject unsupported formats or rates")]]
    virtual Result<void> configure(const AudioFormat& format, double playbackRate) = 0;
    [[nodiscard("tempo processing can fail or buffer input without immediate output")]]
    virtual Result<AudioTempoOutput> process(AudioBufferView input) = 0;
    [[nodiscard("tempo drain must be inspected so EOF does not truncate buffered audio")]]
    virtual Result<AudioTempoOutput> drain() = 0;
    virtual void reset() noexcept = 0;
};

} // namespace media_sdk::runtime
