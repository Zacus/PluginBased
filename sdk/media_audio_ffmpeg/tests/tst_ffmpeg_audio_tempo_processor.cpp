#include "media_sdk/audio/ffmpeg/FfmpegAudioTempoProcessor.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <numbers>
#include <span>
#include <vector>

using namespace std::chrono_literals;

namespace {

constexpr int sampleRate = 48000;
constexpr int channels = 2;

media_sdk::runtime::AudioFormat audioFormat()
{
    return {
        .sampleRate = sampleRate,
        .channels = channels,
        .sampleFormat = media_sdk::runtime::AudioSampleFormat::Float32,
    };
}

std::vector<std::byte> sineBytes(double frequency, std::size_t frames)
{
    std::vector<float> samples(frames * channels);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto phase = 2.0 * std::numbers::pi * frequency
            * static_cast<double>(frame) / sampleRate;
        const auto value = static_cast<float>(std::sin(phase) * 0.5);
        for (int channel = 0; channel < channels; ++channel)
            samples[frame * channels + static_cast<std::size_t>(channel)] = value;
    }

    std::vector<std::byte> bytes(samples.size() * sizeof(float));
    std::memcpy(bytes.data(), samples.data(), bytes.size());
    return bytes;
}

void appendOutput(media_sdk::runtime::AudioTempoOutput output,
                  std::vector<std::byte>& bytes,
                  std::vector<std::chrono::microseconds>& pts)
{
    for (auto& buffer : output.buffers) {
        pts.push_back(buffer.pts);
        bytes.insert(bytes.end(), buffer.bytes.begin(), buffer.bytes.end());
    }
}

struct ProcessedAudio {
    std::vector<std::byte> bytes;
    std::vector<std::chrono::microseconds> pts;
};

ProcessedAudio processInChunks(double playbackRate,
                               std::span<const std::byte> input,
                               std::size_t chunkFrames = 480)
{
    media_sdk::audio::ffmpeg::FfmpegAudioTempoProcessor processor;
    assert(processor.configure(audioFormat(), playbackRate).ok());

    ProcessedAudio output;
    const auto frameBytes = sizeof(float) * channels;
    const auto chunkBytes = chunkFrames * frameBytes;
    std::size_t offset = 0;
    std::uint64_t inputFrames = 0;
    while (offset < input.size()) {
        const auto size = std::min(chunkBytes, input.size() - offset);
        const auto pts = 250ms + std::chrono::microseconds {
            static_cast<std::int64_t>(inputFrames * 1000000 / sampleRate) };
        auto result = processor.process({
            .bytes = input.subspan(offset, size),
            .pts = pts,
            .generation = 7,
            .playbackRate = playbackRate,
        });
        assert(result.ok());
        appendOutput(std::move(result.value()), output.bytes, output.pts);
        offset += size;
        inputFrames += size / frameBytes;
    }

    auto drain = processor.drain();
    assert(drain.ok());
    appendOutput(std::move(drain.value()), output.bytes, output.pts);
    return output;
}

double dominantFrequencyFromZeroCrossings(std::span<const std::byte> bytes)
{
    const auto sampleCount = bytes.size() / sizeof(float);
    std::vector<float> samples(sampleCount);
    std::memcpy(samples.data(), bytes.data(), bytes.size());
    const auto frameCount = samples.size() / channels;
    assert(frameCount > 1000);

    std::size_t crossings = 0;
    float previous = samples[0];
    for (std::size_t frame = 1; frame < frameCount; ++frame) {
        const auto current = samples[frame * channels];
        if (previous <= 0.0f && current > 0.0f)
            ++crossings;
        previous = current;
    }
    return static_cast<double>(crossings) * sampleRate / static_cast<double>(frameCount);
}

void preservesPitchAndScalesDuration(double playbackRate)
{
    constexpr std::size_t sourceFrames = sampleRate;
    const auto source = sineBytes(440.0, sourceFrames);
    const auto output = processInChunks(playbackRate, source);
    const auto outputFrames = output.bytes.size() / (sizeof(float) * channels);
    const auto expectedFrames = static_cast<double>(sourceFrames) / playbackRate;
    assert(std::abs(static_cast<double>(outputFrames) - expectedFrames) / expectedFrames < 0.05);

    const auto frequency = dominantFrequencyFromZeroCrossings(output.bytes);
    assert(std::abs(frequency - 440.0) / 440.0 < 0.02);
    assert(!output.pts.empty());
    assert(output.pts.front() == 250ms);
    for (std::size_t i = 1; i < output.pts.size(); ++i)
        assert(output.pts[i] >= output.pts[i - 1]);
}

void oneTimesBypassPreservesBytesAndPts()
{
    const auto source = sineBytes(440.0, 480);
    media_sdk::audio::ffmpeg::FfmpegAudioTempoProcessor processor;
    assert(processor.configure(audioFormat(), 1.0).ok());
    auto result = processor.process({
        .bytes = source,
        .pts = 123ms,
        .generation = 3,
        .playbackRate = 1.0,
    });
    assert(result.ok());
    assert(result.value().buffers.size() == 1);
    assert(result.value().buffers.front().bytes == source);
    assert(result.value().buffers.front().pts == 123ms);
    auto drain = processor.drain();
    assert(drain.ok());
    assert(drain.value().buffers.empty());
}

void chunkingKeepsDurationAndPtsStable()
{
    const auto source = sineBytes(440.0, sampleRate);
    const auto chunked = processInChunks(1.5, source, 480);
    const auto single = processInChunks(1.5, source, sampleRate);
    const auto chunkedFrames = chunked.bytes.size() / (sizeof(float) * channels);
    const auto singleFrames = single.bytes.size() / (sizeof(float) * channels);
    const auto difference = chunkedFrames > singleFrames
        ? chunkedFrames - singleFrames
        : singleFrames - chunkedFrames;
    assert(difference < sampleRate / 100);
    assert(!chunked.pts.empty());
    assert(!single.pts.empty());
    assert(chunked.pts.front() == single.pts.front());
}

void resetDropsOldTimelineAndRequiresConfigure()
{
    const auto source = sineBytes(440.0, 4800);
    media_sdk::audio::ffmpeg::FfmpegAudioTempoProcessor processor;
    assert(processor.configure(audioFormat(), 0.5).ok());
    auto oldResult = processor.process({
        .bytes = source,
        .pts = 100ms,
        .generation = 1,
        .playbackRate = 0.5,
    });
    assert(oldResult.ok());

    processor.reset();
    auto rejected = processor.process({
        .bytes = source,
        .pts = 5s,
        .generation = 2,
        .playbackRate = 2.0,
    });
    assert(!rejected.ok());

    assert(processor.configure(audioFormat(), 2.0).ok());
    auto newResult = processor.process({
        .bytes = source,
        .pts = 5s,
        .generation = 2,
        .playbackRate = 2.0,
    });
    assert(newResult.ok());
    auto drain = processor.drain();
    assert(drain.ok());
    for (const auto& buffer : newResult.value().buffers)
        assert(buffer.pts >= 5s);
    for (const auto& buffer : drain.value().buffers)
        assert(buffer.pts >= 5s);
}

void rejectsInvalidConfigurationAndInput()
{
    media_sdk::audio::ffmpeg::FfmpegAudioTempoProcessor processor;
    auto format = audioFormat();
    format.sampleFormat = media_sdk::runtime::AudioSampleFormat::Int16;
    assert(!processor.configure(format, 1.5).ok());
    assert(!processor.configure(audioFormat(), 3.0).ok());

    assert(processor.configure(audioFormat(), 1.5).ok());
    const std::vector<std::byte> unaligned(3);
    assert(!processor.process({
        .bytes = unaligned,
        .pts = 0us,
        .generation = 1,
        .playbackRate = 1.5,
    }).ok());
    const auto source = sineBytes(440.0, 480);
    assert(!processor.process({
        .bytes = source,
        .pts = 0us,
        .generation = 1,
        .playbackRate = 1.25,
    }).ok());
}

} // namespace

int main()
{
    preservesPitchAndScalesDuration(0.5);
    preservesPitchAndScalesDuration(0.75);
    preservesPitchAndScalesDuration(1.25);
    preservesPitchAndScalesDuration(1.5);
    preservesPitchAndScalesDuration(2.0);
    oneTimesBypassPreservesBytesAndPts();
    chunkingKeepsDurationAndPtsStable();
    resetDropsOldTimelineAndRequiresConfigure();
    rejectsInvalidConfigurationAndInput();
}
