#pragma once

#include "media_sdk/Error.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <variant>

namespace media_sdk {

enum class PlayerState {
    Stopped,
    Playing,
    Paused,
    Finished,
    Error
};

struct MediaInfo {
    std::chrono::milliseconds duration { 0 };
    int width = 0;
    int height = 0;
    double fps = 0.0;
    int sampleRate = 0;
    int channels = 0;
    std::string formatName;
};

struct MediaInfoEvent {
    MediaInfo info;
};

struct StateChangedEvent {
    PlayerState state = PlayerState::Stopped;
};

struct PositionChangedEvent {
    std::chrono::milliseconds position { 0 };
};

struct ErrorEvent {
    MediaError error;
};

struct EndOfFileEvent {
};

using PlayerEventPayload = std::variant<
    MediaInfoEvent,
    StateChangedEvent,
    PositionChangedEvent,
    ErrorEvent,
    EndOfFileEvent>;

struct PlayerEvent {
    PlayerEventPayload payload;
};

} // namespace media_sdk
