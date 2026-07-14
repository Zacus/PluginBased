#pragma once

#include "DecodeWorker.h"
#include "media_sdk/DecodeFrameSink.h"
#include "media_sdk/PlayerConfig.h"
#include "media_sdk/Result.h"

#include <chrono>
#include <filesystem>

namespace media_sdk {

class PlaybackController
{
public:
    PlaybackController(PlayerConfig config, IEventSink& events, IDecodeFrameSink& frames);
    ~PlaybackController();

    PlaybackController(const PlaybackController&) = delete;
    PlaybackController& operator=(const PlaybackController&) = delete;

    Result<void> open(const std::filesystem::path& path);
    void play();
    void pause();
    void stop();
    Result<void> seek(std::chrono::milliseconds position);
    Result<void> seek(std::chrono::milliseconds position, SeekPlaybackMode mode);
    Result<void> seek(std::chrono::milliseconds position,
                      SeekPlaybackMode mode,
                      SeekRequestId requestId);
    [[nodiscard]] PlayerDiagnostics diagnostics() const;

private:
    DecodeWorker m_worker;
};

} // namespace media_sdk
