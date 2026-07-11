#include "AudioSilence.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>

using namespace std::chrono_literals;

namespace {

void int16SilenceIsZeroFilled()
{
    const auto silence = media_sdk::runtime::makeSilenceBytes({
        .sampleRate = 1000,
        .channels = 2,
        .sampleFormat = media_sdk::runtime::AudioSampleFormat::Int16,
    }, 2ms);

    assert(silence.ok());
    assert(silence.value().size() == 2 * 2 * sizeof(std::int16_t));
    for (const auto byte : silence.value())
        assert(byte == std::byte { 0 });
}

void uint8SilenceUsesUnsignedCenter()
{
    const auto silence = media_sdk::runtime::makeSilenceBytes({
        .sampleRate = 1000,
        .channels = 1,
        .sampleFormat = media_sdk::runtime::AudioSampleFormat::UInt8,
    }, 3ms);

    assert(silence.ok());
    assert(silence.value().size() == 3);
    for (const auto byte : silence.value())
        assert(byte == std::byte { 0x80 });
}

void zeroOrSubFrameDurationReturnsEmptyBuffer()
{
    const auto silence = media_sdk::runtime::makeSilenceBytes({
        .sampleRate = 1000,
        .channels = 2,
        .sampleFormat = media_sdk::runtime::AudioSampleFormat::Float32,
    }, 500us);

    assert(silence.ok());
    assert(silence.value().empty());
}

void invalidFormatFails()
{
    const auto silence = media_sdk::runtime::makeSilenceBytes({
        .sampleRate = 0,
        .channels = 2,
        .sampleFormat = media_sdk::runtime::AudioSampleFormat::Float32,
    }, 1ms);

    assert(!silence.ok());
    assert(silence.error().code == media_sdk::MediaErrorCode::InternalStateError);
}

} // namespace

int main()
{
    int16SilenceIsZeroFilled();
    uint8SilenceUsesUnsignedCenter();
    zeroOrSubFrameDurationReturnsEmptyBuffer();
    invalidFormatFails();
}
