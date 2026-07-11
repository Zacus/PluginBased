#pragma once

#include "media_sdk/Frame.h"

#include <chrono>
#include <cstdint>

namespace media_sdk {

struct VideoPicturePoolSnapshot {
    std::uint64_t acquireCount = 0;
    std::uint64_t reuseCount = 0;
    std::uint64_t allocationCount = 0;
    std::uint64_t transientAllocationCount = 0;
    std::uint64_t highWatermark = 0;
    std::uint64_t retainedCount = 0;
    std::uint64_t inFlightCount = 0;
};

struct DecodeFrameMetadata {
    std::uint64_t sessionId = 0;
    std::uint64_t generation = 0;
    VideoPicturePoolSnapshot videoPicturePool;
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
