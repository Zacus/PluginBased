#pragma once

#include "media_sdk/Error.h"
#include "media_sdk/Result.h"
#include "media_sdk/runtime/AudioOutput.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace media_sdk::runtime {

inline std::size_t bytesPerAudioSample(AudioSampleFormat format)
{
    switch (format) {
    case AudioSampleFormat::UInt8:
        return sizeof(std::uint8_t);
    case AudioSampleFormat::Int16:
        return sizeof(std::int16_t);
    case AudioSampleFormat::Int32:
        return sizeof(std::int32_t);
    case AudioSampleFormat::Float32:
    case AudioSampleFormat::Float32Planar:
        return sizeof(float);
    case AudioSampleFormat::Unknown:
        break;
    }
    return 0;
}

inline Result<std::vector<std::byte>> makeSilenceBytes(
    AudioFormat format,
    std::chrono::microseconds duration)
{
    if (duration <= std::chrono::microseconds::zero())
        return Result<std::vector<std::byte>>::success({});

    const auto bytesPerSample = bytesPerAudioSample(format.sampleFormat);
    if (format.sampleRate <= 0 || format.channels <= 0 || bytesPerSample == 0) {
        return Result<std::vector<std::byte>>::failure({
            .code = MediaErrorCode::InternalStateError,
            .message = "invalid audio format for silence generation",
            .detail = {},
        });
    }

    const auto sampleFrames = (static_cast<std::int64_t>(format.sampleRate) * duration.count()) / 1'000'000;
    if (sampleFrames <= 0)
        return Result<std::vector<std::byte>>::success({});

    const auto byteCount = static_cast<std::size_t>(sampleFrames) *
        static_cast<std::size_t>(format.channels) * bytesPerSample;
    std::vector<std::byte> bytes(byteCount, std::byte { 0 });

    if (format.sampleFormat == AudioSampleFormat::UInt8) {
        for (auto& byte : bytes)
            byte = std::byte { 0x80 };
    }

    return Result<std::vector<std::byte>>::success(std::move(bytes));
}

} // namespace media_sdk::runtime
