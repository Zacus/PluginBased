#pragma once

#include "media_sdk/runtime/AudioTempoProcessor.h"

#include <memory>

namespace media_sdk::audio::ffmpeg {

class FfmpegAudioTempoProcessor final : public runtime::IAudioTempoProcessor
{
public:
    FfmpegAudioTempoProcessor();
    ~FfmpegAudioTempoProcessor() override;

    FfmpegAudioTempoProcessor(const FfmpegAudioTempoProcessor&) = delete;
    FfmpegAudioTempoProcessor& operator=(const FfmpegAudioTempoProcessor&) = delete;

    Result<void> configure(const runtime::AudioFormat& format, double playbackRate) override;
    Result<runtime::AudioTempoOutput> process(runtime::AudioBufferView input) override;
    Result<runtime::AudioTempoOutput> drain() override;
    void reset() noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace media_sdk::audio::ffmpeg
