#include "SeekAudioTrimmer.h"

#include "media_sdk/Frame.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

using namespace std::chrono_literals;

namespace {

std::vector<std::byte> bytesFromInt16(std::vector<std::int16_t> samples)
{
    std::vector<std::byte> bytes(samples.size() * sizeof(std::int16_t));
    std::memcpy(bytes.data(), samples.data(), bytes.size());
    return bytes;
}

std::vector<std::int16_t> int16FromBytes(std::span<const std::byte> bytes)
{
    std::vector<std::int16_t> samples(bytes.size() / sizeof(std::int16_t));
    std::memcpy(samples.data(), bytes.data(), bytes.size());
    return samples;
}

void crossingInt16AudioIsTrimmedToTargetSample()
{
    auto frame = media_sdk::AudioFrame::fromOwnedSamples(
        media_sdk::AudioSampleFormat::Signed16Interleaved,
        1000,
        2,
        1000ms,
        bytesFromInt16({ 1, 2, 3, 4, 5, 6, 7, 8 }));

    const auto result = media_sdk::trimAudioFrameForSeek(frame, 1002ms);

    assert(result.status == media_sdk::SeekAudioTrimStatus::Trimmed);
    assert(result.frame.pts() == 1002ms);
    assert(result.frame.samples().size() == 4 * sizeof(std::int16_t));
    assert((int16FromBytes(result.frame.samples()) == std::vector<std::int16_t> { 5, 6, 7, 8 }));
}

void audioBeforeTargetIsDiscarded()
{
    auto frame = media_sdk::AudioFrame::fromOwnedSamples(
        media_sdk::AudioSampleFormat::Signed16Interleaved,
        1000,
        2,
        1000ms,
        bytesFromInt16({ 1, 2, 3, 4 }));

    const auto result = media_sdk::trimAudioFrameForSeek(frame, 1003ms);

    assert(result.status == media_sdk::SeekAudioTrimStatus::Discard);
}

void audioAfterTargetIsKept()
{
    auto frame = media_sdk::AudioFrame::fromOwnedSamples(
        media_sdk::AudioSampleFormat::Signed16Interleaved,
        1000,
        2,
        1005ms,
        bytesFromInt16({ 1, 2, 3, 4 }));

    const auto result = media_sdk::trimAudioFrameForSeek(frame, 1000ms);

    assert(result.status == media_sdk::SeekAudioTrimStatus::Keep);
    assert(result.frame.pts() == 1005ms);
}

void invalidFormatFails()
{
    auto frame = media_sdk::AudioFrame::fromOwnedSamples(
        media_sdk::AudioSampleFormat::Unknown,
        1000,
        2,
        1000ms,
        {});

    const auto result = media_sdk::trimAudioFrameForSeek(frame, 1000ms);

    assert(result.status == media_sdk::SeekAudioTrimStatus::Invalid);
}

} // namespace

int main()
{
    crossingInt16AudioIsTrimmedToTargetSample();
    audioBeforeTargetIsDiscarded();
    audioAfterTargetIsKept();
    invalidFormatFails();
}
