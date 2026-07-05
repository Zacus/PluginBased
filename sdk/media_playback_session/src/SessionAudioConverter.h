#pragma once

#include "media_sdk/Error.h"
#include "media_sdk/Frame.h"
#include "media_sdk/Result.h"
#include "media_sdk/runtime/AudioOutput.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace media_sdk::session {

struct ConvertedAudioFrame {
    AudioFrame frame;
    runtime::AudioSampleFormat runtimeFormat = runtime::AudioSampleFormat::Unknown;
};

namespace detail {

inline MediaError audioConversionError(std::string message)
{
    return {
        .code = MediaErrorCode::UnsupportedFormat,
        .message = std::move(message),
        .detail = {},
    };
}

template<typename SampleType>
[[nodiscard("audio sample byte size must match the declared input sample type")]]
inline bool sampleBytesAreAligned(std::span<const std::byte> samples)
{
    return samples.size() % sizeof(SampleType) == 0;
}

inline std::int16_t readInt16(const std::byte* source)
{
    std::int16_t value = 0;
    std::memcpy(&value, source, sizeof(value));
    return value;
}

inline std::int32_t readInt32(const std::byte* source)
{
    std::int32_t value = 0;
    std::memcpy(&value, source, sizeof(value));
    return value;
}

inline AudioFrame makeFloat32FrameFromBytes(const AudioFrame& source,
                                            std::vector<std::byte> samples)
{
    return AudioFrame::fromOwnedSamples(AudioSampleFormat::Float32Interleaved,
                                        source.sampleRate(),
                                        source.channels(),
                                        source.pts(),
                                        std::move(samples));
}

inline std::vector<std::byte> bytesFromFloatSamples(const std::vector<float>& samples)
{
    std::vector<std::byte> output(samples.size() * sizeof(float));
    if (!output.empty())
        std::memcpy(output.data(), samples.data(), output.size());
    return output;
}

} // namespace detail

[[nodiscard("audio conversion can fail for unsupported SDK sample formats")]]
inline Result<ConvertedAudioFrame> convertToRuntimeAudioFrame(const AudioFrame& frame)
{
    const auto samples = frame.samples();
    if (frame.sampleRate() <= 0 || frame.channels() <= 0) {
        return Result<ConvertedAudioFrame>::failure(
            detail::audioConversionError("Audio frame has invalid sample rate or channel count"));
    }

    if (frame.sampleFormat() == AudioSampleFormat::Float32Interleaved) {
        std::vector<std::byte> copied(samples.begin(), samples.end());
        return Result<ConvertedAudioFrame>::success({
            .frame = detail::makeFloat32FrameFromBytes(frame, std::move(copied)),
            .runtimeFormat = runtime::AudioSampleFormat::Float32,
        });
    }

    if (frame.sampleFormat() == AudioSampleFormat::Signed16Interleaved) {
        if (!detail::sampleBytesAreAligned<std::int16_t>(samples)) {
            return Result<ConvertedAudioFrame>::failure(
                detail::audioConversionError("Signed16 audio sample bytes are not sample-aligned"));
        }

        std::vector<float> converted(samples.size() / sizeof(std::int16_t));
        for (std::size_t i = 0; i < converted.size(); ++i) {
            const auto value = detail::readInt16(samples.data() + i * sizeof(std::int16_t));
            converted[i] = static_cast<float>(value) / 32768.0f;
        }

        return Result<ConvertedAudioFrame>::success({
            .frame = detail::makeFloat32FrameFromBytes(frame,
                                                       detail::bytesFromFloatSamples(converted)),
            .runtimeFormat = runtime::AudioSampleFormat::Float32,
        });
    }

    if (frame.sampleFormat() == AudioSampleFormat::Signed32Interleaved) {
        if (!detail::sampleBytesAreAligned<std::int32_t>(samples)) {
            return Result<ConvertedAudioFrame>::failure(
                detail::audioConversionError("Signed32 audio sample bytes are not sample-aligned"));
        }

        std::vector<float> converted(samples.size() / sizeof(std::int32_t));
        for (std::size_t i = 0; i < converted.size(); ++i) {
            const auto value = detail::readInt32(samples.data() + i * sizeof(std::int32_t));
            converted[i] = static_cast<float>(value) / 2147483648.0f;
        }

        return Result<ConvertedAudioFrame>::success({
            .frame = detail::makeFloat32FrameFromBytes(frame,
                                                       detail::bytesFromFloatSamples(converted)),
            .runtimeFormat = runtime::AudioSampleFormat::Float32,
        });
    }

    return Result<ConvertedAudioFrame>::failure(
        detail::audioConversionError("Unsupported audio sample format for runtime output"));
}

} // namespace media_sdk::session
