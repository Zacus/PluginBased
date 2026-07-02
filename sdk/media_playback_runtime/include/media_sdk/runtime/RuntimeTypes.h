#pragma once

#include "media_sdk/Frame.h"

#include <chrono>
#include <cstdint>

namespace media_sdk::runtime {

using SessionId = std::uint64_t;
using Generation = std::uint64_t;

enum class VideoOutputPolicy {
    PreferNative,
    CpuOnly,
    RequireNative
};

struct RuntimeAudioFrame {
    AudioFrame frame;
    SessionId sessionId = 0;
    Generation generation = 0;
    bool endOfStream = false;
};

struct RuntimeVideoFrame {
    VideoFrame frame;
    SessionId sessionId = 0;
    Generation generation = 0;
    bool endOfStream = false;
};

enum class RuntimeFramePushStatus {
    Accepted,
    Backpressured,
    RejectedGeneration,
    Cancelled,
    Closed
};

struct RuntimeFramePushResult {
    RuntimeFramePushStatus status = RuntimeFramePushStatus::Closed;
    std::chrono::microseconds waitTime { 0 };
};

struct RuntimeDiagnostics {
    std::uint64_t nativeDecoded = 0;
    std::uint64_t nativeAccepted = 0;
    std::uint64_t nativePresented = 0;
    std::uint64_t nativeFallbacks = 0;
    std::uint64_t nativeTextureCreated = 0;
    std::uint64_t nativeTextureFailed = 0;
    std::uint64_t nativeTextureDrawn = 0;
    std::uint64_t cpuDecoded = 0;
    std::uint64_t cpuPresented = 0;
    std::uint64_t cpuCopied = 0;
    std::uint64_t cpuTransferred = 0;
    std::uint64_t cpuMemcpy = 0;
    std::uint64_t hardwareTransfers = 0;
    std::uint64_t audioQueued = 0;
    std::uint64_t audioBackpressureCount = 0;
    std::uint64_t audioQueueHighWatermark = 0;
    std::uint64_t audioWritten = 0;
    std::uint64_t videoQueued = 0;
    std::uint64_t videoBackpressureCount = 0;
    std::uint64_t videoQueueHighWatermark = 0;
    std::uint64_t videoWaited = 0;
    std::uint64_t videoDroppedLate = 0;
    std::uint64_t videoPresented = 0;
    std::uint64_t eofAccepted = 0;
    std::uint64_t eofPresented = 0;
    std::uint64_t decodeFramePushWaitUs = 0;
    std::uint64_t queueAbortCount = 0;
};

struct RuntimeFallbackAction {
    SessionId sessionId = 0;
    Generation generation = 0;
    std::chrono::microseconds resumePosition { 0 };
    VideoOutputPolicy outputPolicy = VideoOutputPolicy::CpuOnly;
    bool preferNativeVideoFrames = false;
};

struct RuntimeTimeline {
    SessionId sessionId = 0;
    Generation generation = 0;
};

} // namespace media_sdk::runtime
