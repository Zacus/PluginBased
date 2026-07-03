#pragma once

#include "media_sdk/DecodeFrameSink.h"
#include "media_sdk/Player.h"
#include "media_sdk/Result.h"
#include "media_sdk/runtime/RuntimePlayer.h"
#include "media_sdk/session/PlaybackSession.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>

namespace media_sdk::session::detail {

class IPlaybackSessionCorePlayer {
public:
    virtual ~IPlaybackSessionCorePlayer() = default;

    [[nodiscard("open result determines whether core playback can proceed")]]
    virtual Result<void> open(const std::filesystem::path& path) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    [[nodiscard("seek result determines whether core accepted the seek command")]]
    virtual Result<void> seek(std::chrono::milliseconds position) = 0;
};

class IPlaybackSessionRuntimePlayer {
public:
    virtual ~IPlaybackSessionRuntimePlayer() = default;

    [[nodiscard("open result determines whether runtime queues and outputs are usable")]]
    virtual Result<void> open() = 0;
    [[nodiscard("frame enqueue result controls decoder backpressure and cancellation")]]
    virtual runtime::RuntimeFramePushResult enqueueAudio(runtime::RuntimeAudioFrame frame) = 0;
    [[nodiscard("frame enqueue result controls decoder backpressure and cancellation")]]
    virtual runtime::RuntimeFramePushResult enqueueVideo(runtime::RuntimeVideoFrame frame) = 0;
    virtual void enqueueEndOfStream(runtime::SessionId sessionId, runtime::Generation generation) = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void seek(std::chrono::microseconds position) = 0;
    virtual void completeSeek(runtime::SessionId sessionId, runtime::Generation generation) = 0;
    virtual void stop() = 0;
    [[nodiscard("diagnostics verify queue, fallback, native, and clock behavior")]]
    virtual runtime::RuntimeDiagnostics diagnostics() const = 0;
    [[nodiscard("timeline is required for stale callback checks")]]
    virtual runtime::RuntimeTimeline timeline() const = 0;
};

struct PlaybackSessionFactories {
    using CoreFactory = std::function<std::shared_ptr<IPlaybackSessionCorePlayer>(
        PlayerConfig,
        IEventSink&,
        IDecodeFrameSink&)>;
    using RuntimeFactory = std::function<std::shared_ptr<IPlaybackSessionRuntimePlayer>(
        runtime::RuntimePlayerConfig,
        runtime::RuntimePlayerDependencies)>;

    CoreFactory createCore;
    RuntimeFactory createRuntime;
};

struct PlaybackSessionTestHooks {
    static std::unique_ptr<PlaybackSession> create(PlaybackSessionConfig config,
                                                   PlaybackSessionDependencies dependencies,
                                                   PlaybackSessionFactories factories);
};

} // namespace media_sdk::session::detail
