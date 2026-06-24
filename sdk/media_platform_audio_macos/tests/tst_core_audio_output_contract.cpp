#include "media_sdk/platform/macos/CoreAudioAudioOutput.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <vector>

using namespace std::chrono_literals;

namespace {

void openWriteClockPauseResumeFlushAndCloseAreDeterministic()
{
    media_sdk::platform::macos::CoreAudioAudioOutput output;

    assert(output.open({
        .sampleRate = 48000,
        .channels = 2,
        .sampleFormat = media_sdk::runtime::AudioSampleFormat::Float32,
    }).ok());

    auto snapshot = output.clock();
    assert(snapshot.valid);
    assert(snapshot.generation == 1);
    assert(!snapshot.paused);

    std::vector<std::byte> bytes(48000 * 2 * 4 / 10);
    assert(output.write({
        .bytes = bytes,
        .pts = 250ms,
        .generation = 1,
    }).ok());

    snapshot = output.clock();
    assert(snapshot.position == 250ms);
    assert(snapshot.queuedDuration == 100ms);

    output.pause();
    assert(output.clock().paused);
    output.resume();
    assert(!output.clock().paused);

    output.flush();
    snapshot = output.clock();
    assert(snapshot.valid);
    assert(snapshot.generation == 2);
    assert(snapshot.queuedDuration == 0us);

    assert(output.write({
        .bytes = bytes,
        .pts = 500ms,
        .generation = 1,
    }).ok() == false);
    assert(output.write({
        .bytes = bytes,
        .pts = 500ms,
        .generation = 2,
    }).ok());

    output.close();
    assert(!output.clock().valid);
}

void invalidOpenAndClosedWriteFail()
{
    media_sdk::platform::macos::CoreAudioAudioOutput output;
    assert(!output.open({
        .sampleRate = 0,
        .channels = 2,
        .sampleFormat = media_sdk::runtime::AudioSampleFormat::Float32,
    }).ok());

    std::vector<std::byte> bytes(128);
    assert(!output.write({
        .bytes = bytes,
        .pts = 0us,
        .generation = 1,
    }).ok());
}

} // namespace

int main()
{
    openWriteClockPauseResumeFlushAndCloseAreDeterministic();
    invalidOpenAndClosedWriteFail();
}
