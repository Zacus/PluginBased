#pragma once

#include "media_sdk/Result.h"
#include "media_sdk/runtime/AudioOutput.h"
#include "media_sdk/runtime/RuntimeTypes.h"
#include "media_sdk/runtime/VideoPresenter.h"

#include <chrono>
#include <cstddef>
#include <memory>

namespace media_sdk::runtime {

struct RuntimeSyncConfig {
    std::chrono::microseconds submitLeadTime { std::chrono::milliseconds(2) };
    std::chrono::microseconds lateDropThreshold { std::chrono::milliseconds(100) };
    std::chrono::microseconds maxScheduledWait { std::chrono::milliseconds(40) };
    int maxConsecutiveDropsBeforeForceRender = 8;
};

struct RuntimePlayerConfig {
    std::size_t audioQueueCapacity = 32;
    std::size_t videoQueueCapacity = 8;
    VideoOutputPolicy outputPolicy = VideoOutputPolicy::PreferNative;
    RuntimeSyncConfig syncConfig {};
};

struct RuntimePlayerDependencies {
    IAudioOutput* audioOutput = nullptr;
    IVideoPresenter* videoPresenter = nullptr;
};

class RuntimePlayer final : private IVideoPresenterEvents
{
public:
    RuntimePlayer(RuntimePlayerConfig config, RuntimePlayerDependencies dependencies);
    ~RuntimePlayer() override;

    RuntimePlayer(const RuntimePlayer&) = delete;
    RuntimePlayer& operator=(const RuntimePlayer&) = delete;

    Result<void> open();
    void enqueueAudio(RuntimeAudioFrame frame);
    void enqueueVideo(RuntimeVideoFrame frame);
    void enqueueEndOfStream(SessionId sessionId, Generation generation);
    void pause();
    void resume();
    void seek(std::chrono::microseconds position);
    void stop();
    RuntimeDiagnostics diagnostics() const;

private:
    void onPresentComplete(PresentCompletion completion) override;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace media_sdk::runtime
