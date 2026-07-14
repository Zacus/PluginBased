#pragma once

#include "media_sdk/MediaEvents.h"
#include "media_sdk/PlayerConfig.h"
#include "media_sdk/runtime/RuntimePlayer.h"

namespace media_sdk::session {

class ISessionEvents {
public:
    virtual ~ISessionEvents() = default;
    virtual void onEvent(const PlayerEvent& event) = 0;
    // Observation-only runtime snapshot. Consumers must not drive playback policy from it.
    virtual void onRuntimeDiagnostics(runtime::RuntimeDiagnostics diagnostics)
    {
        (void)diagnostics;
    }
    virtual void onNativeRenderingFailed() {}
};

struct PlaybackSessionConfig {
    PlayerConfig core {};
    runtime::RuntimePlayerConfig runtime {};
    bool preferNativeVideoFrames = true;
};

struct PlaybackSessionDependencies {
    runtime::IAudioOutput* audioOutput = nullptr;
    runtime::IVideoPresenter* videoPresenter = nullptr;
    ISessionEvents* events = nullptr;
    runtime::IAudioTempoProcessor* audioTempoProcessor = nullptr;
};

} // namespace media_sdk::session
