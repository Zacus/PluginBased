#include "SeekAudioTrimmer.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace media_sdk {

std::size_t seekBytesPerSample(AudioSampleFormat format)
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

SeekAudioTrimResult trimAudioFrameForSeek(const AudioFrame& frame,
                                          std::chrono::microseconds target)
{
    const auto bytesPerSample = seekBytesPerSample(frame.sampleFormat());
    if (bytesPerSample == 0 || frame.sampleRate() <= 0 || frame.channels() <= 0)
        return { .status = SeekAudioTrimStatus::Invalid };

    // Interleaved audio must be trimmed by whole sample frames across all channels.
    const auto bytesPerAudioFrame = bytesPerSample * static_cast<std::size_t>(frame.channels());
    if (bytesPerAudioFrame == 0 || frame.samples().size() % bytesPerAudioFrame != 0)
        return { .status = SeekAudioTrimStatus::Invalid };

    if (frame.pts() >= target)
        return { .status = SeekAudioTrimStatus::Keep, .frame = frame };

    const auto deltaUs = target - frame.pts();
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
