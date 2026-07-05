#pragma once

#include "media_sdk/Result.h"
#include "media_sdk/runtime/RuntimeTypes.h"
#include "media_sdk/session/PlaybackSessionTypes.h"

#include <chrono>
#include <filesystem>
#include <memory>

namespace media_sdk::session {

namespace detail {
struct PlaybackSessionFactories;
struct PlaybackSessionTestHooks;
} // namespace detail

class PlaybackSession final {
public:
    PlaybackSession(PlaybackSessionConfig config,
                    PlaybackSessionDependencies dependencies);
    ~PlaybackSession();

    PlaybackSession(const PlaybackSession&) = delete;
    PlaybackSession& operator=(const PlaybackSession&) = delete;

    [[nodiscard("open result determines whether playback session is usable")]]
    Result<void> open(const std::filesystem::path& path);
    void play();
    void pause();
    void stop();
    void setAudioControls(runtime::RuntimeAudioControls controls);
    [[nodiscard("seek result determines whether the seek command was accepted")]]
    Result<void> seek(std::chrono::milliseconds position);
    [[nodiscard("timeline is required for stale callback checks")]]
    runtime::RuntimeTimeline timeline() const;
    [[nodiscard("diagnostics verify queue, fallback, native, and clock behavior")]]
    runtime::RuntimeDiagnostics diagnostics() const;

private:
    friend struct detail::PlaybackSessionTestHooks;

    PlaybackSession(PlaybackSessionConfig config,
                    PlaybackSessionDependencies dependencies,
                    std::shared_ptr<detail::PlaybackSessionFactories> factories);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace media_sdk::session
