#pragma once

#include "media_sdk/DecodeFrameSink.h"
#include "media_sdk/Diagnostics.h"
#include "media_sdk/MediaEvents.h"
#include "media_sdk/PlayerConfig.h"
#include "media_sdk/Result.h"
#include "media_sdk/SeekTypes.h"

#include <chrono>
#include <filesystem>
#include <memory>

namespace media_sdk {

class IEventSink
{
public:
    virtual ~IEventSink() = default;
    // control events only; decoded audio/video frames use IDecodeFrameSink.
    virtual void onEvent(const PlayerEvent& event) = 0;
};

class Player
{
public:
    explicit Player(PlayerConfig config, IEventSink& events, IDecodeFrameSink& frames);
    ~Player();

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    Player(Player&&) noexcept;
    Player& operator=(Player&&) noexcept;

    [[nodiscard("inspect the open result before starting playback")]]
    Result<void> open(const std::filesystem::path& path);
    void play();
    void pause();
    void stop();
    [[nodiscard("inspect the seek result before assuming the seek was accepted")]]
    Result<void> seek(std::chrono::milliseconds position);
    [[nodiscard("inspect the seek result before assuming the seek was accepted")]]
    Result<void> seek(std::chrono::milliseconds position, SeekPlaybackMode mode);
    [[nodiscard]] PlayerDiagnostics diagnostics() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace media_sdk
