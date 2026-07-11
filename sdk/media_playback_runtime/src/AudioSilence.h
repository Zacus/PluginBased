#pragma once

#include "media_sdk/Error.h"
#include "media_sdk/Result.h"
#include "media_sdk/runtime/AudioOutput.h"

#include <chrono>
#include <cstddef>
#include <vector>

namespace media_sdk::runtime {

std::size_t bytesPerAudioSample(AudioSampleFormat format);

Result<std::vector<std::byte>> makeSilenceBytes(
    AudioFormat format,
    std::chrono::microseconds duration);

} // namespace media_sdk::runtime
