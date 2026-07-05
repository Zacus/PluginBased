#include "SessionAudioConverter.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace {

template<typename SampleType>
std::vector<std::byte> bytesFromSamples(const std::vector<SampleType>& samples)
{
    std::vector<std::byte> output(samples.size() * sizeof(SampleType));
    if (!output.empty())
        std::memcpy(output.data(), samples.data(), output.size());
    return output;
}

media_sdk::AudioFrame makeAudioFrame(media_sdk::AudioSampleFormat sampleFormat,
                                     int sampleRate,
                                     int channels,
                                     std::chrono::microseconds pts,
                                     std::vector<std::byte> samples)
{
    return media_sdk::AudioFrame::fromOwnedSamples(sampleFormat,
                                                   sampleRate,
                                                   channels,
                                                   pts,
                                                   std::move(samples));
}

std::vector<float> floatSamples(const media_sdk::AudioFrame& frame)
{
    const auto samples = frame.samples();
    assert(samples.size() % sizeof(float) == 0);
    std::vector<float> result(samples.size() / sizeof(float));
    if (!result.empty())
        std::memcpy(result.data(), samples.data(), samples.size());
    return result;
}

void assertNearly(float actual, float expected)
{
    assert(std::fabs(actual - expected) < 0.000001f);
}

void float32InterleavedIsPreserved()
{
    const std::vector<float> source { -1.0f, -0.25f, 0.0f, 0.75f };
    const auto sourceBytes = bytesFromSamples(source);
    auto frame = makeAudioFrame(media_sdk::AudioSampleFormat::Float32Interleaved,
                                48000,
                                2,
                                123ms,
                                sourceBytes);

    const auto converted = media_sdk::session::convertToRuntimeAudioFrame(frame);

    assert(converted.ok());
    assert(converted.value().runtimeFormat == media_sdk::runtime::AudioSampleFormat::Float32);
    assert(converted.value().frame.sampleFormat()
           == media_sdk::AudioSampleFormat::Float32Interleaved);
    assert(converted.value().frame.sampleRate() == 48000);
    assert(converted.value().frame.channels() == 2);
    assert(converted.value().frame.pts() == 123ms);
    assert(converted.value().frame.samples().size() == sourceBytes.size());
    assert(std::memcmp(converted.value().frame.samples().data(),
                       sourceBytes.data(),
                       sourceBytes.size()) == 0);
}

void signed16InterleavedConvertsToFloat32()
{
    const std::vector<std::int16_t> source {
        static_cast<std::int16_t>(-32768),
        static_cast<std::int16_t>(-16384),
        0,
        16384,
        32767,
    };
    auto frame = makeAudioFrame(media_sdk::AudioSampleFormat::Signed16Interleaved,
                                44100,
                                1,
                                25ms,
                                bytesFromSamples(source));

    const auto converted = media_sdk::session::convertToRuntimeAudioFrame(frame);

    assert(converted.ok());
    const auto samples = floatSamples(converted.value().frame);
    assert(samples.size() == source.size());
    assertNearly(samples[0], -1.0f);
    assertNearly(samples[1], -0.5f);
    assertNearly(samples[2], 0.0f);
    assertNearly(samples[3], 0.5f);
    assertNearly(samples[4], 32767.0f / 32768.0f);
}

void signed32InterleavedConvertsToFloat32()
{
    const std::vector<std::int32_t> source {
        std::numeric_limits<std::int32_t>::min(),
        static_cast<std::int32_t>(-1073741824),
        0,
        1073741824,
        std::numeric_limits<std::int32_t>::max(),
    };
    auto frame = makeAudioFrame(media_sdk::AudioSampleFormat::Signed32Interleaved,
                                48000,
                                2,
                                40ms,
                                bytesFromSamples(source));

    const auto converted = media_sdk::session::convertToRuntimeAudioFrame(frame);

    assert(converted.ok());
    const auto samples = floatSamples(converted.value().frame);
    assert(samples.size() == source.size());
    assertNearly(samples[0], -1.0f);
    assertNearly(samples[1], -0.5f);
    assertNearly(samples[2], 0.0f);
    assertNearly(samples[3], 0.5f);
    assertNearly(samples[4], 2147483647.0f / 2147483648.0f);
}

void unknownFormatFails()
{
    auto frame = makeAudioFrame(media_sdk::AudioSampleFormat::Unknown,
                                48000,
                                2,
                                0us,
                                {});

    const auto converted = media_sdk::session::convertToRuntimeAudioFrame(frame);

    assert(!converted.ok());
    assert(converted.error().code == media_sdk::MediaErrorCode::UnsupportedFormat);
}

void invalidAudioFormatFails()
{
    auto frame = makeAudioFrame(media_sdk::AudioSampleFormat::Float32Interleaved,
                                0,
                                2,
                                0us,
                                bytesFromSamples(std::vector<float> { 0.0f }));

    const auto converted = media_sdk::session::convertToRuntimeAudioFrame(frame);

    assert(!converted.ok());
    assert(converted.error().code == media_sdk::MediaErrorCode::UnsupportedFormat);
}

void unalignedSigned16BytesFail()
{
    auto frame = makeAudioFrame(media_sdk::AudioSampleFormat::Signed16Interleaved,
                                48000,
                                2,
                                0us,
                                std::vector<std::byte> { std::byte { 1 } });

    const auto converted = media_sdk::session::convertToRuntimeAudioFrame(frame);

    assert(!converted.ok());
    assert(converted.error().code == media_sdk::MediaErrorCode::UnsupportedFormat);
}

} // namespace

int main()
{
    float32InterleavedIsPreserved();
    signed16InterleavedConvertsToFloat32();
    signed32InterleavedConvertsToFloat32();
    unknownFormatFails();
    invalidAudioFormatFails();
    unalignedSigned16BytesFail();
}
