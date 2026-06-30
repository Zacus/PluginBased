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

    [[nodiscard("inspect the CoreAudio open result before writing audio")]]
    Result<void> open(const runtime::AudioFormat& format) override;
    [[nodiscard("CoreAudio writes can reject stale generations or closed outputs")]]
    Result<void> write(runtime::AudioBufferView buffer) override;
    [[nodiscard("CoreAudio clock snapshots drive runtime A/V sync")]]
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
