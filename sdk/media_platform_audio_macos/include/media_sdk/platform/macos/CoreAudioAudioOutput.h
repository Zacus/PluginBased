#pragma once

#include "media_sdk/runtime/AudioOutput.h"

#include <memory>

namespace media_sdk::platform::macos {

class CoreAudioAudioOutput final : public runtime::IAudioOutput
{
public:
    CoreAudioAudioOutput();
    ~CoreAudioAudioOutput() override;

    CoreAudioAudioOutput(const CoreAudioAudioOutput&) = delete;
    CoreAudioAudioOutput& operator=(const CoreAudioAudioOutput&) = delete;

    Result<void> open(const runtime::AudioFormat& format) override;
    Result<void> write(runtime::AudioBufferView buffer) override;
    runtime::ClockSnapshot clock() const override;
    void pause() override;
    void resume() override;
    void flush() override;
    void close() override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace media_sdk::platform::macos
