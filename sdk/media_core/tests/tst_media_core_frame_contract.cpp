#include "media_sdk/Frame.h"
#include "media_sdk/DecodeFrameSink.h"
#include "media_sdk/MediaEvents.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <variant>
#include <vector>

using namespace std::chrono_literals;

namespace {

struct StorageProbe {
    explicit StorageProbe(bool& destroyed)
        : destroyed(destroyed)
    {
    }

    ~StorageProbe()
    {
        destroyed = true;
    }

    bool& destroyed;
};

class RecordingFrameSink final : public media_sdk::IDecodeFrameSink {
public:
    media_sdk::DecodeFramePushResult pushAudio(
        media_sdk::AudioFrame frame,
        media_sdk::DecodeFrameMetadata metadata) override
    {
        ++audioCount;
        lastSessionId = metadata.sessionId;
        lastGeneration = metadata.generation;
        lastAudioPts = frame.pts();
        return { .status = media_sdk::DecodeFramePushStatus::Accepted };
    }

    media_sdk::DecodeFramePushResult pushVideo(
        media_sdk::VideoFrame frame,
        media_sdk::DecodeFrameMetadata metadata) override
    {
        ++videoCount;
        lastSessionId = metadata.sessionId;
        lastGeneration = metadata.generation;
        lastVideoPts = frame.pts();
        return { .status = media_sdk::DecodeFramePushStatus::Accepted };
    }

    int audioCount = 0;
    int videoCount = 0;
    std::uint64_t lastSessionId = 0;
    std::uint64_t lastGeneration = 0;
    std::chrono::microseconds lastAudioPts { 0 };
    std::chrono::microseconds lastVideoPts { 0 };
};

void testVideoFrameMetadataAndStorage()
{
    bool destroyed = false;
    auto storage = std::make_shared<StorageProbe>(destroyed);
    std::vector<std::byte> yPlane(16);
    std::vector<std::byte> uPlane(4);
    std::vector<std::byte> vPlane(4);

    media_sdk::PlaneView planes[] = {
        { yPlane.data(), 4, 4, 4 },
        { uPlane.data(), 2, 2, 2 },
        { vPlane.data(), 2, 2, 2 },
    };

    media_sdk::VideoFrame frame({
        .width = 4,
        .height = 4,
        .pixelFormat = media_sdk::PixelFormat::Yuv420P,
        .colorRange = media_sdk::ColorRange::Full,
        .colorSpace = media_sdk::ColorSpace::Bt709,
        .pts = 42'000us,
        .planes = std::span<const media_sdk::PlaneView>(planes),
        .nativeHandle = {},
        .storage = storage,
    });

    assert(frame.storage() == storage);
    storage.reset();
    assert(!destroyed);
    assert(frame.width() == 4);
    assert(frame.height() == 4);
    assert(frame.pixelFormat() == media_sdk::PixelFormat::Yuv420P);
    assert(frame.colorRange() == media_sdk::ColorRange::Full);
    assert(frame.colorSpace() == media_sdk::ColorSpace::Bt709);
    assert(frame.pts() == 42'000us);
    assert(frame.planes().size() == 3);
    assert(frame.planes()[0].stride == 4);
    assert(frame.hasStorage());

    auto copy = frame;
    assert(!destroyed);
    frame = {};
    assert(!destroyed);
    copy = {};
    assert(destroyed);
}

void testNativeHandleMetadata()
{
    int nativeObject = 7;
    media_sdk::NativeHandle handle {
        .kind = media_sdk::NativeHandleKind::VideoToolboxPixelBuffer,
        .handle = &nativeObject,
        .pixelFormat = 875704438,
    };

    media_sdk::VideoFrame frame({
        .width = 1920,
        .height = 1080,
        .pixelFormat = media_sdk::PixelFormat::Native,
        .colorRange = media_sdk::ColorRange::Limited,
        .colorSpace = media_sdk::ColorSpace::Bt709,
        .pts = 120'000us,
        .planes = {},
        .nativeHandle = handle,
        .storage = std::make_shared<int>(nativeObject),
    });

    assert(frame.nativeHandle().kind == media_sdk::NativeHandleKind::VideoToolboxPixelBuffer);
    assert(frame.nativeHandle().handle == &nativeObject);
    assert(frame.nativeHandle().pixelFormat == 875704438);
}

void testAudioFrameAndEvents()
{
    std::vector<std::byte> samples(128);
    media_sdk::AudioFrame frame({
        .sampleFormat = media_sdk::AudioSampleFormat::Float32Interleaved,
        .sampleRate = 48'000,
        .channels = 2,
        .pts = 15'000us,
        .samples = std::span<const std::byte>(samples),
        .storage = std::make_shared<std::vector<std::byte>>(samples),
    });

    assert(frame.sampleFormat() == media_sdk::AudioSampleFormat::Float32Interleaved);
    assert(frame.sampleRate() == 48'000);
    assert(frame.channels() == 2);
    assert(frame.pts() == 15'000us);
    assert(frame.samples().size() == samples.size());

    media_sdk::PlayerEvent videoEvent {
        .metadata = { .sessionId = 7, .generation = 3 },
        .payload = media_sdk::VideoFrameEvent { media_sdk::VideoFrame {} }
    };
    media_sdk::PlayerEvent audioEvent {
        .metadata = { .sessionId = 7, .generation = 3 },
        .payload = media_sdk::AudioFrameEvent { media_sdk::AudioFrame {} }
    };
    media_sdk::PlayerEvent mediaInfoEvent {
        .metadata = { .sessionId = 7, .generation = 3 },
        .payload = media_sdk::MediaInfoEvent {
            media_sdk::MediaInfo {
                .sampleRate = 48'000,
                .channels = 2,
                .channelLayoutMask = 3,
            }
        }
    };

    assert(videoEvent.metadata.sessionId == 7);
    assert(videoEvent.metadata.generation == 3);
    assert(std::holds_alternative<media_sdk::VideoFrameEvent>(videoEvent.payload));
    assert(audioEvent.metadata.sessionId == 7);
    assert(audioEvent.metadata.generation == 3);
    assert(std::holds_alternative<media_sdk::AudioFrameEvent>(audioEvent.payload));
    assert(mediaInfoEvent.metadata.sessionId == 7);
    assert(mediaInfoEvent.metadata.generation == 3);
    const auto& mediaInfo = std::get<media_sdk::MediaInfoEvent>(mediaInfoEvent.payload).info;
    assert(mediaInfo.sampleRate == 48'000);
    assert(mediaInfo.channels == 2);
    assert(mediaInfo.channelLayoutMask == 3);
}

void testAudioFrameCanOwnMovedSamples()
{
    std::vector<std::byte> samples {
        std::byte { 0x01 },
        std::byte { 0x02 },
        std::byte { 0x03 },
        std::byte { 0x04 },
    };

    media_sdk::AudioFrame frame = media_sdk::AudioFrame::fromOwnedSamples(
        media_sdk::AudioSampleFormat::Signed16Interleaved,
        44'100,
        2,
        1234us,
        std::move(samples));

    assert(frame.sampleFormat() == media_sdk::AudioSampleFormat::Signed16Interleaved);
    assert(frame.sampleRate() == 44'100);
    assert(frame.channels() == 2);
    assert(frame.pts() == 1234us);
    assert(frame.samples().size() == 4);
    assert(frame.samples()[0] == std::byte { 0x01 });
    assert(frame.samples()[3] == std::byte { 0x04 });
}

void testDecodeFrameSinkContract()
{
    RecordingFrameSink sink;
    auto audio = media_sdk::AudioFrame::fromOwnedSamples(
        media_sdk::AudioSampleFormat::Float32Interleaved,
        48'000,
        2,
        1'000us,
        std::vector<std::byte> {});

    const auto result = sink.pushAudio(std::move(audio), {
        .sessionId = 7,
        .generation = 3,
    });

    assert(result.status == media_sdk::DecodeFramePushStatus::Accepted);
    assert(sink.audioCount == 1);
    assert(sink.lastSessionId == 7);
    assert(sink.lastGeneration == 3);
    assert(sink.lastAudioPts == 1'000us);
}

}

int main()
{
    testVideoFrameMetadataAndStorage();
    testNativeHandleMetadata();
    testAudioFrameAndEvents();
    testAudioFrameCanOwnMovedSamples();
    testDecodeFrameSinkContract();
    return 0;
}
