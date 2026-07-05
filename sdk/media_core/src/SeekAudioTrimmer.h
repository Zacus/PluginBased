#pragma once

#include "media_sdk/Frame.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

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

[[nodiscard]] inline std::size_t seekBytesPerSample(AudioSampleFormat format)
{
    switch (format) {
    case AudioSampleFormat::Float32Interleaved:
    case AudioSampleFormat::Signed32Interleaved:
        return 4;
    case AudioSampleFormat::Signed16Interleaved:
        return 2;
    case AudioSampleFormat::Unknown:
        return 0;
    }
    return 0;
}

[[nodiscard("caller must decide whether to keep, trim, discard, or treat audio as invalid")]]
inline SeekAudioTrimResult trimAudioFrameForSeek(const AudioFrame& frame,
                                                std::chrono::microseconds target)
{
    const auto bytesPerSample = seekBytesPerSample(frame.sampleFormat());
    if (bytesPerSample == 0 || frame.sampleRate() <= 0 || frame.channels() <= 0)
        return { .status = SeekAudioTrimStatus::Invalid };

    // interleaved 音频必须按“一个时间点的所有声道”整体裁剪，不能截断单个声道样本。
    const auto bytesPerAudioFrame = bytesPerSample * static_cast<std::size_t>(frame.channels());
    if (bytesPerAudioFrame == 0 || frame.samples().size() % bytesPerAudioFrame != 0)
        return { .status = SeekAudioTrimStatus::Invalid };

    if (frame.pts() >= target)
        return { .status = SeekAudioTrimStatus::Keep, .frame = frame };

    const auto deltaUs = target - frame.pts();
    // 向上取整到第一个不早于 target 的音频帧，避免把目标点前的 samples 送入 runtime。
    const auto trimFrames = static_cast<std::size_t>(
        (deltaUs.count() * static_cast<std::int64_t>(frame.sampleRate()) + 999999) / 1000000);
    const auto totalFrames = frame.samples().size() / bytesPerAudioFrame;
    if (trimFrames >= totalFrames)
        return { .status = SeekAudioTrimStatus::Discard };

    const auto trimBytes = trimFrames * bytesPerAudioFrame;
    std::vector<std::byte> samples(
        frame.samples().begin() + static_cast<std::ptrdiff_t>(trimBytes),
        frame.samples().end());
    const auto newPts = frame.pts() + std::chrono::microseconds {
        static_cast<std::int64_t>(trimFrames) * 1000000 / frame.sampleRate()
    };

    return {
        .status = SeekAudioTrimStatus::Trimmed,
        .frame = AudioFrame::fromOwnedSamples(
            frame.sampleFormat(),
            frame.sampleRate(),
            frame.channels(),
            newPts,
            std::move(samples)),
    };
}

} // namespace media_sdk
