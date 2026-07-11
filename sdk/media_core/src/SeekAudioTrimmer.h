#pragma once

#include "media_sdk/Frame.h"

#include <chrono>
#include <cstddef>

namespace media_sdk {

enum class SeekAudioTrimStatus {
    Keep,
    Trimmed,
    Discard,
    Invalid
};

struct SeekAudioTrimResult {
    SeekAudioTrimStatus status = SeekAudioTrimStatus::Invalid;
    AudioFrame frame;
};

[[nodiscard]] std::size_t seekBytesPerSample(AudioSampleFormat format);

[[nodiscard("caller must decide whether to keep, trim, discard, or treat audio as invalid")]]
SeekAudioTrimResult trimAudioFrameForSeek(const AudioFrame& frame,
                                          std::chrono::microseconds target);

} // namespace media_sdk
