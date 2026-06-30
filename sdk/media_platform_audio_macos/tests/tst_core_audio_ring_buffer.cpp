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

media_sdk::runtime::AudioFormat int16AudioFormat()
{
    return {
        .sampleRate = 48000,
        .channels = 2,
        .sampleFormat = media_sdk::runtime::AudioSampleFormat::Int16,
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
    media_sdk::platform::macos::CoreAudioRingBuffer buffer(16);
    buffer.configure(audioFormat(), 1);

    const auto input = bytes({ 1, 2, 3, 4, 5, 6, 7, 8,
                               9, 10, 11, 12, 13, 14, 15, 16 });
    assert(buffer.write({
        .bytes = input,
        .pts = 10ms,
        .generation = 1,
    }));

    std::vector<std::byte> output(8);
    const auto read = buffer.read(output);
    assert(read.copiedBytes == 8);
    assert(read.silenceBytes == 0);
    assert(output == bytes({ 1, 2, 3, 4, 5, 6, 7, 8 }));

    output.assign(8, std::byte { 0 });
    const auto secondRead = buffer.read(output);
    assert(secondRead.copiedBytes == 8);
    assert(secondRead.silenceBytes == 0);
    assert(output == bytes({ 9, 10, 11, 12, 13, 14, 15, 16 }));
}

void readSilenceWhenUnderrun()
{
    media_sdk::platform::macos::CoreAudioRingBuffer buffer(16);
    buffer.configure(audioFormat(), 1);

    const auto input = bytes({ 9, 8, 7, 6, 5, 4, 3, 2 });
    assert(buffer.write({
        .bytes = input,
        .pts = 20ms,
        .generation = 1,
    }));

    std::vector<std::byte> output(16, std::byte { 7 });
    const auto read = buffer.read(output);
    assert(read.copiedBytes == 8);
    assert(read.silenceBytes == 8);
    assert(output == bytes({ 9, 8, 7, 6, 5, 4, 3, 2,
                             0, 0, 0, 0, 0, 0, 0, 0 }));
}

void writeAndReadAcrossRingWrap()
{
    media_sdk::platform::macos::CoreAudioRingBuffer buffer(24);
    buffer.configure(audioFormat(), 1);

    const auto first = bytes({ 1, 2, 3, 4, 5, 6, 7, 8,
                               9, 10, 11, 12, 13, 14, 15, 16 });
    assert(buffer.write({
        .bytes = first,
        .pts = 20ms,
        .generation = 1,
    }));

    std::vector<std::byte> partial(8);
    const auto firstRead = buffer.read(partial);
    assert(firstRead.copiedBytes == 8);
    assert(firstRead.silenceBytes == 0);
    assert(partial == bytes({ 1, 2, 3, 4, 5, 6, 7, 8 }));

    const auto second = bytes({ 17, 18, 19, 20, 21, 22, 23, 24,
                                25, 26, 27, 28, 29, 30, 31, 32 });
    assert(buffer.write({
        .bytes = second,
        .pts = 30ms,
        .generation = 1,
    }));

    std::vector<std::byte> output(24);
    const auto secondRead = buffer.read(output);
    assert(secondRead.copiedBytes == 24);
    assert(secondRead.silenceBytes == 0);
    assert(output == bytes({ 9, 10, 11, 12, 13, 14, 15, 16,
                             17, 18, 19, 20, 21, 22, 23, 24,
                             25, 26, 27, 28, 29, 30, 31, 32 }));
}

void underrunSilenceDoesNotAdvancePlaybackClock()
{
    media_sdk::platform::macos::CoreAudioRingBuffer buffer(48000);
    buffer.configure(audioFormat(), 1);

    std::vector<std::byte> input(48000 * 2 * 4 / 100);
    assert(buffer.write({
        .bytes = input,
        .pts = 20ms,
        .generation = 1,
    }));

    std::vector<std::byte> output(input.size() * 3);
    const auto read = buffer.read(output);
    assert(read.copiedBytes == input.size());
    assert(read.silenceBytes == input.size() * 2);

    const auto snapshot = buffer.clock();
    assert(snapshot.position == 30ms);
    assert(snapshot.queuedDuration == 0us);
}

void flushClearsQueuedBytesAndIncrementsGeneration()
{
    media_sdk::platform::macos::CoreAudioRingBuffer buffer(8);
    buffer.configure(audioFormat(), 1);

    const auto input = bytes({ 1, 2, 3, 4, 5, 6, 7, 8 });
    assert(buffer.write({
        .bytes = input,
        .pts = 30ms,
        .generation = 1,
    }));

    buffer.flush();
    auto snapshot = buffer.clock();
    assert(snapshot.generation == 2);
    assert(snapshot.queuedDuration == 0us);

    std::vector<std::byte> output(8, std::byte { 7 });
    const auto read = buffer.read(output);
    assert(read.copiedBytes == 0);
    assert(read.silenceBytes == 8);
    assert(output == bytes({ 0, 0, 0, 0, 0, 0, 0, 0 }));

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

void flushRejectsBlockedWriterFromOldGeneration()
{
    media_sdk::platform::macos::CoreAudioRingBuffer buffer(8);
    buffer.configure(audioFormat(), 1);

    const auto initial = bytes({ 1, 2, 3, 4, 5, 6, 7, 8 });
    assert(buffer.write({
        .bytes = initial,
        .pts = 30ms,
        .generation = 1,
    }));

    const auto blocked = bytes({ 9, 10, 11, 12, 13, 14, 15, 16 });
    auto result = std::async(std::launch::async, [&buffer, blocked]() {
        return buffer.write({
            .bytes = blocked,
            .pts = 40ms,
            .generation = 1,
        });
    });

    assert(result.wait_for(20ms) == std::future_status::timeout);
    buffer.flush();
    assert(result.wait_for(1s) == std::future_status::ready);
    assert(!result.get());

    const auto afterFlush = bytes({ 21, 22, 23, 24, 25, 26, 27, 28 });
    assert(buffer.write({
        .bytes = afterFlush,
        .pts = 50ms,
        .generation = 2,
    }));

    std::vector<std::byte> output(8);
    const auto read = buffer.read(output);
    assert(read.copiedBytes == 8);
    assert(read.silenceBytes == 0);
    assert(output == afterFlush);
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

void queuedDurationUsesConfiguredSampleFormat()
{
    media_sdk::platform::macos::CoreAudioRingBuffer buffer(48000);
    buffer.configure(int16AudioFormat(), 1);

    std::vector<std::byte> input(48000 * 2 * 2 / 10);
    assert(buffer.write({
        .bytes = input,
        .pts = 70ms,
        .generation = 1,
    }));

    const auto snapshot = buffer.clock();
    assert(snapshot.position == 70ms);
    assert(snapshot.queuedDuration == 100ms);
}

void writeRejectsNonFrameAlignedBuffers()
{
    media_sdk::platform::macos::CoreAudioRingBuffer buffer(16);
    buffer.configure(audioFormat(), 1);

    const auto input = bytes({ 1, 2, 3 });
    assert(!buffer.write({
        .bytes = input,
        .pts = 80ms,
        .generation = 1,
    }));

    std::vector<std::byte> output(8, std::byte { 7 });
    const auto read = buffer.read(output);
    assert(read.copiedBytes == 0);
    assert(read.silenceBytes == 8);
    assert(output == bytes({ 0, 0, 0, 0, 0, 0, 0, 0 }));
}

void readConsumesOnlyCompletePcmFrames()
{
    media_sdk::platform::macos::CoreAudioRingBuffer buffer(24);
    buffer.configure(audioFormat(), 1);

    const auto input = bytes({ 1, 2, 3, 4, 5, 6, 7, 8,
                               9, 10, 11, 12, 13, 14, 15, 16 });
    assert(buffer.write({
        .bytes = input,
        .pts = 90ms,
        .generation = 1,
    }));

    std::vector<std::byte> output(9, std::byte { 7 });
    const auto read = buffer.read(output);
    assert(read.copiedBytes == 8);
    assert(read.silenceBytes == 1);
    assert(output == bytes({ 1, 2, 3, 4, 5, 6, 7, 8, 0 }));

    std::vector<std::byte> remaining(8);
    const auto secondRead = buffer.read(remaining);
    assert(secondRead.copiedBytes == 8);
    assert(secondRead.silenceBytes == 0);
    assert(remaining == bytes({ 9, 10, 11, 12, 13, 14, 15, 16 }));
}

void closeWakesBlockedWriters()
{
    media_sdk::platform::macos::CoreAudioRingBuffer buffer(8);
    buffer.configure(audioFormat(), 1);

    const auto initial = bytes({ 1, 2, 3, 4, 5, 6, 7, 8 });
    assert(buffer.write({
        .bytes = initial,
        .pts = 60ms,
        .generation = 1,
    }));

    const auto blocked = bytes({ 9, 10, 11, 12, 13, 14, 15, 16 });
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
    writeAndReadAcrossRingWrap();
    underrunSilenceDoesNotAdvancePlaybackClock();
    flushClearsQueuedBytesAndIncrementsGeneration();
    flushRejectsBlockedWriterFromOldGeneration();
    queuedDurationUsesFormatByteRate();
    queuedDurationUsesConfiguredSampleFormat();
    writeRejectsNonFrameAlignedBuffers();
    readConsumesOnlyCompletePcmFrames();
    closeWakesBlockedWriters();
}
