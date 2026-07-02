#pragma once

#include "media_sdk/Frame.h"

#include <chrono>
#include <cstdint>

namespace media_sdk {

struct DecodeFrameMetadata {
    std::uint64_t sessionId = 0;
    std::uint64_t generation = 0;
};

enum class DecodeFramePushStatus {
    Accepted,
    Backpressured,
    StaleGeneration,
    Cancelled,
    Closed
};

struct DecodeFramePushResult {
    DecodeFramePushStatus status = DecodeFramePushStatus::Closed;
    std::chrono::microseconds waitTime { 0 };
};

class IDecodeFrameSink
{
public:
    virtual ~IDecodeFrameSink() = default;

    [[nodiscard("frame push result determines whether decode can continue, retry, or stop")]]
    virtual DecodeFramePushResult pushAudio(AudioFrame frame, DecodeFrameMetadata metadata) = 0;

    [[nodiscard("frame push result determines whether decode can continue, retry, or stop")]]
    virtual DecodeFramePushResult pushVideo(VideoFrame frame, DecodeFrameMetadata metadata) = 0;
};

} // namespace media_sdk
