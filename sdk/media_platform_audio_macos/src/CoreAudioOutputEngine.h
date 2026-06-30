#pragma once

#include "AudioRenderDevice.h"
#include "CoreAudioRingBuffer.h"

#include <memory>
#include <mutex>

namespace media_sdk::platform::macos {

class CoreAudioOutputEngine final {
public:
    explicit CoreAudioOutputEngine(std::unique_ptr<IAudioRenderDevice> device);
    ~CoreAudioOutputEngine();

    CoreAudioOutputEngine(const CoreAudioOutputEngine&) = delete;
    CoreAudioOutputEngine& operator=(const CoreAudioOutputEngine&) = delete;

    [[nodiscard("inspect the CoreAudio open result before writing audio")]]
    Result<void> open(const runtime::AudioFormat& format);
    [[nodiscard("CoreAudio writes can reject stale generations or closed outputs")]]
    Result<void> write(runtime::AudioBufferView buffer);
    [[nodiscard("CoreAudio clock snapshots drive runtime A/V sync")]]
    runtime::ClockSnapshot clock() const;
    void pause();
    void resume();
    void flush();
    void close();

private:
    static void renderCallback(void* context, std::span<std::byte> destination) noexcept;
    void render(std::span<std::byte> destination) noexcept;

    mutable std::mutex m_mutex;
    CoreAudioRingBuffer m_ringBuffer;
    std::unique_ptr<IAudioRenderDevice> m_device;
    runtime::AudioFormat m_format {};
    runtime::Generation m_generation = 1;
    bool m_open = false;
    bool m_paused = false;
    bool m_running = false;
};

} // namespace media_sdk::platform::macos
