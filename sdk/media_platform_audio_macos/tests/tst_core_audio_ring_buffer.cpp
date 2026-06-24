#include "CoreAudioRingBuffer.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <future>
#include <vector>

using namespace std::chrono_literals;

namespace {

media_sdk::runtime::AudioFormat audioFormat()
{
    return {
        .sampleRate = 48000,
        .channels = 2,
        .sampleFormat = media_sdk::runtime::AudioSampleFormat::Float32,
    };
}

std::vector<std::byte> bytes(std::initializer_list<unsigned int> values)
{
    std::vector<std::byte> result;
    result.reserve(values.size());
    for (const auto value : values)
        result.push_back(static_cast<std::byte>(value));
    return result;
}

void writeAndReadPreserveByteOrder()
{
    media_sdk::platform::macos::CoreAudioRingBuffer buffer(8);
    buffer.configure(audioFormat(), 1);

    const auto input = bytes({ 1, 2, 3, 4, 5, 6 });
    assert(buffer.write({
        .bytes = input,
        .pts = 10ms,
        .generation = 1,
    }));

    std::vector<std::byte> output(4);
    const auto read = buffer.read(output);
    assert(read.copiedBytes == 4);
    assert(read.silenceBytes == 0);
    assert(output == bytes({ 1, 2, 3, 4 }));

    output.assign(2, std::byte { 0 });
    const auto secondRead = buffer.read(output);
    assert(secondRead.copiedBytes == 2);
    assert(secondRead.silenceBytes == 0);
    assert(output == bytes({ 5, 6 }));
}

void readSilenceWhenUnderrun()
{
    media_sdk::platform::macos::CoreAudioRingBuffer buffer(8);
    buffer.configure(audioFormat(), 1);

    const auto input = bytes({ 9, 8 });
    assert(buffer.write({
        .bytes = input,
        .pts = 20ms,
        .generation = 1,
    }));

    std::vector<std::byte> output(6, std::byte { 7 });
    const auto read = buffer.read(output);
    assert(read.copiedBytes == 2);
    assert(read.silenceBytes == 4);
    assert(output == bytes({ 9, 8, 0, 0, 0, 0 }));
}

void flushClearsQueuedBytesAndIncrementsGeneration()
{
    media_sdk::platform::macos::CoreAudioRingBuffer buffer(8);
    buffer.configure(audioFormat(), 1);

    const auto input = bytes({ 1, 2, 3, 4 });
    assert(buffer.write({
        .bytes = input,
        .pts = 30ms,
        .generation = 1,
    }));

    buffer.flush();
    auto snapshot = buffer.clock();
    assert(snapshot.generation == 2);
    assert(snapshot.queuedDuration == 0us);

    std::vector<std::byte> output(4, std::byte { 7 });
    const auto read = buffer.read(output);
    assert(read.copiedBytes == 0);
    assert(read.silenceBytes == 4);
    assert(output == bytes({ 0, 0, 0, 0 }));

    assert(!buffer.write({
        .bytes = input,
        .pts = 30ms,
        .generation = 1,
    }));
    assert(buffer.write({
        .bytes = input,
        .pts = 40ms,
        .generation = 2,
    }));
}

void queuedDurationUsesFormatByteRate()
{
    media_sdk::platform::macos::CoreAudioRingBuffer buffer(48000);
    buffer.configure(audioFormat(), 1);

    std::vector<std::byte> input(48000 * 2 * 4 / 10);
    assert(buffer.write({
        .bytes = input,
        .pts = 50ms,
        .generation = 1,
    }));

    auto snapshot = buffer.clock();
    assert(snapshot.position == 50ms);
    assert(snapshot.queuedDuration == 100ms);

    std::vector<std::byte> output(48000 * 2 * 4 / 20);
    buffer.read(output);
    snapshot = buffer.clock();
    assert(snapshot.position == 100ms);
    assert(snapshot.queuedDuration == 50ms);
}

void closeWakesBlockedWriters()
{
    media_sdk::platform::macos::CoreAudioRingBuffer buffer(4);
    buffer.configure(audioFormat(), 1);

    const auto initial = bytes({ 1, 2, 3, 4 });
    assert(buffer.write({
        .bytes = initial,
        .pts = 60ms,
        .generation = 1,
    }));

    const auto blocked = bytes({ 5, 6, 7, 8 });
    auto result = std::async(std::launch::async, [&buffer, blocked]() {
        return buffer.write({
            .bytes = blocked,
            .pts = 70ms,
            .generation = 1,
        });
    });

    assert(result.wait_for(20ms) == std::future_status::timeout);
    buffer.close();
    assert(result.wait_for(1s) == std::future_status::ready);
    assert(!result.get());
}

} // namespace

int main()
{
    writeAndReadPreserveByteOrder();
    readSilenceWhenUnderrun();
    flushClearsQueuedBytesAndIncrementsGeneration();
    queuedDurationUsesFormatByteRate();
    closeWakesBlockedWriters();
}
