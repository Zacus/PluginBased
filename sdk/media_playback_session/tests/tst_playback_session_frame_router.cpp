#include "SessionFrameRouter.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace {

struct RecordingRuntimeSink {
    media_sdk::runtime::RuntimeFramePushResult nextAudioResult {
        .status = media_sdk::runtime::RuntimeFramePushStatus::Accepted,
    };
    media_sdk::runtime::RuntimeFramePushResult nextVideoResult {
        .status = media_sdk::runtime::RuntimeFramePushStatus::Accepted,
    };
    int audioPushCount = 0;
    int videoPushCount = 0;
    media_sdk::runtime::RuntimeAudioFrame lastAudio {};
    media_sdk::runtime::RuntimeVideoFrame lastVideo {};

    [[nodiscard("test must inspect mapped frame push result")]]
    media_sdk::runtime::RuntimeFramePushResult enqueueAudio(
        media_sdk::runtime::RuntimeAudioFrame frame)
    {
        ++audioPushCount;
        lastAudio = std::move(frame);
        return nextAudioResult;
    }

    [[nodiscard("test must inspect mapped frame push result")]]
    media_sdk::runtime::RuntimeFramePushResult enqueueVideo(
        media_sdk::runtime::RuntimeVideoFrame frame)
    {
        ++videoPushCount;
        lastVideo = std::move(frame);
        return nextVideoResult;
    }
};

media_sdk::EventMetadata coreTimeline(std::uint64_t sessionId, std::uint64_t generation)
{
    return {
        .sessionId = sessionId,
        .generation = generation,
    };
}

media_sdk::DecodeFrameMetadata decodeMetadata(
    std::uint64_t sessionId,
    std::uint64_t generation,
    media_sdk::VideoPicturePoolSnapshot picturePool = {})
{
    return {
        .sessionId = sessionId,
        .generation = generation,
        .videoPicturePool = picturePool,
    };
}

media_sdk::runtime::RuntimeTimeline runtimeTimeline(std::uint64_t sessionId,
                                                    std::uint64_t generation)
{
    return {
        .sessionId = sessionId,
        .generation = generation,
    };
}

std::vector<std::byte> bytesFromFloats(const std::vector<float>& samples)
{
    std::vector<std::byte> bytes(samples.size() * sizeof(float));
    if (!bytes.empty())
        std::memcpy(bytes.data(), samples.data(), bytes.size());
    return bytes;
}

media_sdk::AudioFrame makeAudioFrame(std::chrono::microseconds pts = 42ms)
{
    return media_sdk::AudioFrame::fromOwnedSamples(
        media_sdk::AudioSampleFormat::Float32Interleaved,
        48000,
        2,
        pts,
        bytesFromFloats({ 0.25f, -0.5f }));
}

media_sdk::VideoFrame makeVideoFrame(std::chrono::microseconds pts = 84ms)
{
    const auto storage = std::make_shared<int>(7);
    return media_sdk::VideoFrame(media_sdk::VideoFrameDesc {
        .width = 1920,
        .height = 1080,
        .pixelFormat = media_sdk::PixelFormat::Nv12,
        .colorRange = media_sdk::ColorRange::Limited,
        .colorSpace = media_sdk::ColorSpace::Bt709,
        .pts = pts,
        .storage = storage,
    });
}

media_sdk::session::SessionTimeline acceptedTimeline()
{
    media_sdk::session::SessionTimeline timeline;
    timeline.acceptCoreTimeline(coreTimeline(10, 3), runtimeTimeline(20, 7));
    return timeline;
}

void matchingCoreTimelineAudioFrameCallsRuntimeEnqueueAudio()
{
    auto timeline = acceptedTimeline();
    RecordingRuntimeSink runtime;
    media_sdk::session::BasicSessionFrameRouter<RecordingRuntimeSink> router(runtime, timeline);

    const auto result = router.pushAudio(makeAudioFrame(), decodeMetadata(10, 3));

    assert(result.status == media_sdk::DecodeFramePushStatus::Accepted);
    assert(runtime.audioPushCount == 1);
    assert(runtime.videoPushCount == 0);
    assert(runtime.lastAudio.sessionId == 20);
    assert(runtime.lastAudio.generation == 7);
    assert(runtime.lastAudio.frame.sampleFormat() == media_sdk::AudioSampleFormat::Float32Interleaved);
    assert(runtime.lastAudio.frame.sampleRate() == 48000);
    assert(runtime.lastAudio.frame.channels() == 2);
    assert(runtime.lastAudio.frame.pts() == 42ms);
    assert(runtime.lastAudio.frame.samples().size() == 2 * sizeof(float));
}

void matchingCoreTimelineVideoFrameCallsRuntimeEnqueueVideo()
{
    auto timeline = acceptedTimeline();
    RecordingRuntimeSink runtime;
    media_sdk::session::BasicSessionFrameRouter<RecordingRuntimeSink> router(runtime, timeline);

    const auto result = router.pushVideo(makeVideoFrame(), decodeMetadata(10, 3, {
        .acquireCount = 12,
        .reuseCount = 9,
        .allocationCount = 3,
        .transientAllocationCount = 1,
        .highWatermark = 4,
        .retainedCount = 3,
        .inFlightCount = 2,
    }));

    assert(result.status == media_sdk::DecodeFramePushStatus::Accepted);
    assert(runtime.audioPushCount == 0);
    assert(runtime.videoPushCount == 1);
    assert(runtime.lastVideo.sessionId == 20);
    assert(runtime.lastVideo.generation == 7);
    assert(runtime.lastVideo.frame.width() == 1920);
    assert(runtime.lastVideo.frame.height() == 1080);
    assert(runtime.lastVideo.frame.pixelFormat() == media_sdk::PixelFormat::Nv12);
    assert(runtime.lastVideo.frame.pts() == 84ms);
    assert(runtime.lastVideo.frame.hasStorage());
    assert(runtime.lastVideo.videoPicturePool.acquireCount == 12);
    assert(runtime.lastVideo.videoPicturePool.reuseCount == 9);
    assert(runtime.lastVideo.videoPicturePool.allocationCount == 3);
    assert(runtime.lastVideo.videoPicturePool.transientAllocationCount == 1);
    assert(runtime.lastVideo.videoPicturePool.highWatermark == 4);
    assert(runtime.lastVideo.videoPicturePool.retainedCount == 3);
    assert(runtime.lastVideo.videoPicturePool.inFlightCount == 2);
}

void staleCoreGenerationMapsToStaleGeneration()
{
    auto timeline = acceptedTimeline();
    RecordingRuntimeSink runtime;
    media_sdk::session::BasicSessionFrameRouter<RecordingRuntimeSink> router(runtime, timeline);

    const auto audioResult = router.pushAudio(makeAudioFrame(), decodeMetadata(10, 2));
    const auto videoResult = router.pushVideo(makeVideoFrame(), decodeMetadata(10, 4));

    assert(audioResult.status == media_sdk::DecodeFramePushStatus::StaleGeneration);
    assert(videoResult.status == media_sdk::DecodeFramePushStatus::StaleGeneration);
    assert(runtime.audioPushCount == 0);
    assert(runtime.videoPushCount == 0);
}

void runtimeBackpressuredMapsToDecodeBackpressured()
{
    auto timeline = acceptedTimeline();
    RecordingRuntimeSink runtime;
    runtime.nextAudioResult = {
        .status = media_sdk::runtime::RuntimeFramePushStatus::Backpressured,
        .waitTime = 1200us,
    };
    media_sdk::session::BasicSessionFrameRouter<RecordingRuntimeSink> router(runtime, timeline);

    const auto result = router.pushAudio(makeAudioFrame(), decodeMetadata(10, 3));

    assert(result.status == media_sdk::DecodeFramePushStatus::Backpressured);
    assert(result.waitTime == 1200us);
}

void runtimeRejectedGenerationMapsToDecodeStaleGeneration()
{
    auto timeline = acceptedTimeline();
    RecordingRuntimeSink runtime;
    runtime.nextVideoResult = {
        .status = media_sdk::runtime::RuntimeFramePushStatus::RejectedGeneration,
        .waitTime = 5us,
    };
    media_sdk::session::BasicSessionFrameRouter<RecordingRuntimeSink> router(runtime, timeline);

    const auto result = router.pushVideo(makeVideoFrame(), decodeMetadata(10, 3));

    assert(result.status == media_sdk::DecodeFramePushStatus::StaleGeneration);
    assert(result.waitTime == 5us);
}

void runtimeCancelledMapsToDecodeCancelled()
{
    auto timeline = acceptedTimeline();
    RecordingRuntimeSink runtime;
    runtime.nextAudioResult = {
        .status = media_sdk::runtime::RuntimeFramePushStatus::Cancelled,
        .waitTime = 10us,
    };
    media_sdk::session::BasicSessionFrameRouter<RecordingRuntimeSink> router(runtime, timeline);

    const auto result = router.pushAudio(makeAudioFrame(), decodeMetadata(10, 3));

    assert(result.status == media_sdk::DecodeFramePushStatus::Cancelled);
    assert(result.waitTime == 10us);
}

void runtimeClosedMapsToDecodeClosed()
{
    auto timeline = acceptedTimeline();
    RecordingRuntimeSink runtime;
    runtime.nextVideoResult = {
        .status = media_sdk::runtime::RuntimeFramePushStatus::Closed,
        .waitTime = 8us,
    };
    media_sdk::session::BasicSessionFrameRouter<RecordingRuntimeSink> router(runtime, timeline);

    const auto result = router.pushVideo(makeVideoFrame(), decodeMetadata(10, 3));

    assert(result.status == media_sdk::DecodeFramePushStatus::Closed);
    assert(result.waitTime == 8us);
}

} // namespace

int main()
{
    matchingCoreTimelineAudioFrameCallsRuntimeEnqueueAudio();
    matchingCoreTimelineVideoFrameCallsRuntimeEnqueueVideo();
    staleCoreGenerationMapsToStaleGeneration();
    runtimeBackpressuredMapsToDecodeBackpressured();
    runtimeRejectedGenerationMapsToDecodeStaleGeneration();
    runtimeCancelledMapsToDecodeCancelled();
    runtimeClosedMapsToDecodeClosed();
}
