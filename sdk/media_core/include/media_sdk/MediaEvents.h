#pragma once

#include "media_sdk/Diagnostics.h"
#include "media_sdk/Error.h"

#include <chrono>
#include <cstdint>
#include <optional>
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
    std::uint64_t channelLayoutMask = 0;
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

struct SeekCompletedEvent {
    std::chrono::milliseconds position { 0 };
    std::chrono::milliseconds requestedPosition { position };
    std::optional<std::chrono::milliseconds> firstAudioPts;
    std::optional<std::chrono::milliseconds> firstVideoPts;
    bool exact = true;
    bool audioGap = false;
};

struct ErrorEvent {
    MediaError error;
};

struct EndOfFileEvent {
};

struct DecodePerformanceEvent {
    std::string decoderName;
    std::int64_t decodedVideoFrames = 0;
    std::int64_t transferAverageUs = 0;
    std::int64_t transferMaxUs = 0;
    std::int64_t normalizeAverageUs = 0;
    std::int64_t normalizeMaxUs = 0;
    std::int64_t framePushAverageWaitUs = 0;
    std::int64_t framePushMaxWaitUs = 0;
    VideoPicturePoolSnapshot videoPicturePool;
    DecoderBufferPoolSnapshot decoderBufferPool;
};

struct EventMetadata {
    std::uint64_t sessionId = 0;
    std::uint64_t generation = 0;
};

using PlayerEventPayload = std::variant<
    MediaInfoEvent,
    StateChangedEvent,
    PositionChangedEvent,
    SeekCompletedEvent,
    ErrorEvent,
    EndOfFileEvent,
    DecodePerformanceEvent>;

struct PlayerEvent {
    EventMetadata metadata;
    PlayerEventPayload payload;
};

} // namespace media_sdk
