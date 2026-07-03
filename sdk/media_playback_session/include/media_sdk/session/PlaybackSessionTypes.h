#pragma once

#include "media_sdk/MediaEvents.h"
#include "media_sdk/PlayerConfig.h"
#include "media_sdk/runtime/RuntimePlayer.h"

namespace media_sdk::session {

class ISessionEvents {
public:
    virtual ~ISessionEvents() = default;
    virtual void onEvent(const PlayerEvent& event) = 0;
    virtual void onRuntimeDiagnostics(runtime::RuntimeDiagnostics diagnostics)
    {
        (void)diagnostics;
    }
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
};

} // namespace media_sdk::session
