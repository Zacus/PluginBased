#include "media_sdk/Player.h"
#include "media_sdk/DecodeFrameSink.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

extern "C" {
#include <libavcodec/codec_id.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/pixfmt.h>
}

using namespace std::chrono_literals;

namespace {

void writeLe16(std::ofstream& out, std::uint16_t value)
{
    out.put(static_cast<char>(value & 0xff));
    out.put(static_cast<char>((value >> 8) & 0xff));
}

void writeLe32(std::ofstream& out, std::uint32_t value)
{
    writeLe16(out, static_cast<std::uint16_t>(value & 0xffff));
    writeLe16(out, static_cast<std::uint16_t>((value >> 16) & 0xffff));
}

std::filesystem::path writeTinyWav()
{
    const int sampleRate = 8000;
    const int channels = 1;
    const int bitsPerSample = 16;
    const int frameCount = sampleRate / 2;
    const int dataBytes = frameCount * channels * (bitsPerSample / 8);

    const auto path = std::filesystem::temp_directory_path()
        / ("media_sdk_core_stage6_" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".wav");

    std::ofstream out(path, std::ios::binary);
    assert(out);
    out.write("RIFF", 4);
    writeLe32(out, static_cast<std::uint32_t>(36 + dataBytes));
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeLe32(out, 16);
    writeLe16(out, 1);
    writeLe16(out, channels);
    writeLe32(out, sampleRate);
    writeLe32(out, sampleRate * channels * (bitsPerSample / 8));
    writeLe16(out, channels * (bitsPerSample / 8));
    writeLe16(out, bitsPerSample);
    out.write("data", 4);
    writeLe32(out, static_cast<std::uint32_t>(dataBytes));

    for (int i = 0; i < frameCount; ++i)
    {
        const double phase = static_cast<double>(i) * 440.0 * 2.0 * 3.14159265358979323846
            / static_cast<double>(sampleRate);
        writeLe16(out, static_cast<std::uint16_t>(
                           static_cast<std::int16_t>(std::sin(phase) * 12000.0)));
    }

    return path;
}

std::filesystem::path writeAudioFirstVideoSample()
{
    const auto path = std::filesystem::temp_directory_path()
        / ("media_sdk_core_audio_first_video_" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".nut");

    AVFormatContext* rawContext = nullptr;
    assert(avformat_alloc_output_context2(&rawContext, nullptr, "nut", path.string().c_str()) >= 0);
    assert(rawContext);

    auto* audio = avformat_new_stream(rawContext, nullptr);
    assert(audio);
    audio->id = 0;
    audio->time_base = AVRational { 1, 8000 };
    audio->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    audio->codecpar->codec_id = AV_CODEC_ID_PCM_S16LE;
    audio->codecpar->format = AV_SAMPLE_FMT_S16;
    audio->codecpar->sample_rate = 8000;
    audio->codecpar->ch_layout = AV_CHANNEL_LAYOUT_MONO;
    audio->codecpar->bits_per_coded_sample = 16;
    audio->codecpar->block_align = 2;
    audio->codecpar->bit_rate = 128000;

    auto* video = avformat_new_stream(rawContext, nullptr);
    assert(video);
    video->id = 1;
    video->time_base = AVRational { 1, 25 };
    video->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    video->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
    video->codecpar->format = AV_PIX_FMT_YUYV422;
    video->codecpar->width = 16;
    video->codecpar->height = 16;

    assert(avio_open(&rawContext->pb, path.string().c_str(), AVIO_FLAG_WRITE) >= 0);
    assert(avformat_write_header(rawContext, nullptr) >= 0);

    constexpr int audioSamplesPerPacket = 160;
    std::vector<std::byte> audioSamples(audioSamplesPerPacket * 2);
    for (int packetIndex = 0; packetIndex < 8; ++packetIndex) {
        AVPacket* packet = av_packet_alloc();
        assert(packet);
        assert(av_new_packet(packet, static_cast<int>(audioSamples.size())) >= 0);
        std::memcpy(packet->data, audioSamples.data(), audioSamples.size());
        packet->stream_index = audio->index;
        packet->pts = packetIndex * audioSamplesPerPacket;
        packet->dts = packet->pts;
        packet->duration = audioSamplesPerPacket;
        assert(av_interleaved_write_frame(rawContext, packet) >= 0);
        av_packet_free(&packet);
    }

    std::vector<std::byte> videoFrame(16 * 16 * 2);
    for (std::size_t i = 0; i < videoFrame.size(); i += 4) {
        videoFrame[i] = std::byte { 0x10 };
        videoFrame[i + 1] = std::byte { 0x80 };
        videoFrame[i + 2] = std::byte { 0x10 };
        videoFrame[i + 3] = std::byte { 0x80 };
    }
    AVPacket* packet = av_packet_alloc();
    assert(packet);
    assert(av_new_packet(packet, static_cast<int>(videoFrame.size())) >= 0);
    std::memcpy(packet->data, videoFrame.data(), videoFrame.size());
    packet->stream_index = video->index;
    packet->pts = 0;
    packet->dts = 0;
    packet->duration = 1;
    packet->flags |= AV_PKT_FLAG_KEY;
    assert(av_interleaved_write_frame(rawContext, packet) >= 0);
    av_packet_free(&packet);

    assert(av_write_trailer(rawContext) >= 0);
    avio_closep(&rawContext->pb);
    avformat_free_context(rawContext);
    return path;
}

std::filesystem::path writeLongGopVideoSample()
{
    const auto path = std::filesystem::temp_directory_path()
        / ("media_sdk_core_long_gop_video_" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".nut");

    AVFormatContext* rawContext = nullptr;
    assert(avformat_alloc_output_context2(&rawContext, nullptr, "nut", path.string().c_str()) >= 0);
    assert(rawContext);

    auto* video = avformat_new_stream(rawContext, nullptr);
    assert(video);
    video->id = 0;
    video->time_base = AVRational { 1, 25 };
    video->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    video->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
    video->codecpar->format = AV_PIX_FMT_YUYV422;
    video->codecpar->width = 16;
    video->codecpar->height = 16;

    assert(avio_open(&rawContext->pb, path.string().c_str(), AVIO_FLAG_WRITE) >= 0);
    assert(avformat_write_header(rawContext, nullptr) >= 0);

    std::vector<std::byte> videoFrame(16 * 16 * 2);
    for (std::size_t i = 0; i < videoFrame.size(); i += 4) {
        videoFrame[i] = std::byte { 0x10 };
        videoFrame[i + 1] = std::byte { 0x80 };
        videoFrame[i + 2] = std::byte { 0x10 };
        videoFrame[i + 3] = std::byte { 0x80 };
    }

    for (int frameIndex = 0; frameIndex < 6; ++frameIndex)
    {
        AVPacket* packet = av_packet_alloc();
        assert(packet);
        assert(av_new_packet(packet, static_cast<int>(videoFrame.size())) >= 0);
        std::memcpy(packet->data, videoFrame.data(), videoFrame.size());
        packet->stream_index = video->index;
        packet->pts = frameIndex;
        packet->dts = frameIndex;
        packet->duration = 1;
        packet->flags |= AV_PKT_FLAG_KEY;
        assert(av_interleaved_write_frame(rawContext, packet) >= 0);
        av_packet_free(&packet);
    }

    assert(av_write_trailer(rawContext) >= 0);
    avio_closep(&rawContext->pb);
    avformat_free_context(rawContext);
    return path;
}

std::filesystem::path writeSparseKeyframeVideoSample()
{
    const auto path = std::filesystem::temp_directory_path()
        / ("media_sdk_core_sparse_keyframe_video_" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".nut");

    AVFormatContext* rawContext = nullptr;
    assert(avformat_alloc_output_context2(&rawContext, nullptr, "nut", path.string().c_str()) >= 0);
    assert(rawContext);

    auto* video = avformat_new_stream(rawContext, nullptr);
    assert(video);
    video->id = 0;
    video->time_base = AVRational { 1, 25 };
    video->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    video->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
    video->codecpar->format = AV_PIX_FMT_YUYV422;
    video->codecpar->width = 16;
    video->codecpar->height = 16;

    assert(avio_open(&rawContext->pb, path.string().c_str(), AVIO_FLAG_WRITE) >= 0);
    assert(avformat_write_header(rawContext, nullptr) >= 0);

    std::vector<std::byte> videoFrame(16 * 16 * 2);
    for (std::size_t i = 0; i < videoFrame.size(); i += 4) {
        videoFrame[i] = std::byte { 0x10 };
        videoFrame[i + 1] = std::byte { 0x80 };
        videoFrame[i + 2] = std::byte { 0x10 };
        videoFrame[i + 3] = std::byte { 0x80 };
    }

    for (int frameIndex = 0; frameIndex < 310; ++frameIndex)
    {
        AVPacket* packet = av_packet_alloc();
        assert(packet);
        assert(av_new_packet(packet, static_cast<int>(videoFrame.size())) >= 0);
        std::memcpy(packet->data, videoFrame.data(), videoFrame.size());
        packet->stream_index = video->index;
        packet->pts = frameIndex;
        packet->dts = frameIndex;
        packet->duration = 1;
        if (frameIndex == 0)
            packet->flags |= AV_PKT_FLAG_KEY;
        assert(av_interleaved_write_frame(rawContext, packet) >= 0);
        av_packet_free(&packet);
    }

    assert(av_write_trailer(rawContext) >= 0);
    avio_closep(&rawContext->pb);
    avformat_free_context(rawContext);
    return path;
}

class RecordingSink final : public media_sdk::IEventSink
{
public:
    void onEvent(const media_sdk::PlayerEvent& event) override
    {
        {
            std::scoped_lock lock(m_mutex);
            m_events.push_back(event);
        }
        m_cv.notify_all();
    }

    template<typename Predicate>
    bool waitFor(Predicate predicate, std::chrono::milliseconds timeout = 3s)
    {
        std::unique_lock lock(m_mutex);
        return m_cv.wait_for(lock, timeout, [&]() {
            return std::ranges::any_of(m_events, predicate);
        });
    }

    std::vector<media_sdk::PlayerEvent> snapshot() const
    {
        std::scoped_lock lock(m_mutex);
        return m_events;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<media_sdk::PlayerEvent> m_events;
};

class RecordingFrameSink final : public media_sdk::IDecodeFrameSink {
public:
    media_sdk::DecodeFramePushResult pushAudio(
        media_sdk::AudioFrame frame,
        media_sdk::DecodeFrameMetadata metadata) override
    {
        bool shouldBlock = false;
        {
            std::scoped_lock lock(m_mutex);
            shouldBlock = m_blockAudioFrames;
            if (shouldBlock)
                m_audioFrameBlocked = true;
        }
        m_cv.notify_all();

        if (shouldBlock)
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [&]() { return !m_blockAudioFrames; });
        }

        {
            std::scoped_lock lock(m_mutex);
            if (m_cancelAudioFrames)
                return { .status = media_sdk::DecodeFramePushStatus::Cancelled };
        }

        std::scoped_lock lock(m_mutex);
        ++m_audioFrames;
        m_lastSessionId = metadata.sessionId;
        m_lastGeneration = metadata.generation;
        m_lastAudioPts = frame.pts();
        m_cv.notify_all();
        return { .status = media_sdk::DecodeFramePushStatus::Accepted };
    }

    media_sdk::DecodeFramePushResult pushVideo(
        media_sdk::VideoFrame frame,
        media_sdk::DecodeFrameMetadata metadata) override
    {
        bool shouldBlock = false;
        {
            std::scoped_lock lock(m_mutex);
            shouldBlock = m_blockVideoFrames;
            if (shouldBlock)
                m_videoFrameBlocked = true;
        }
        m_cv.notify_all();

        if (shouldBlock)
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [&]() { return !m_blockVideoFrames; });
        }

        std::scoped_lock lock(m_mutex);
        ++m_videoFrames;
        m_lastSessionId = metadata.sessionId;
        m_lastGeneration = metadata.generation;
        m_lastVideoPts = frame.pts();
        m_cv.notify_all();
        return { .status = media_sdk::DecodeFramePushStatus::Accepted };
    }

    bool waitForAudioFrame(std::chrono::milliseconds timeout = 3s)
    {
        std::unique_lock lock(m_mutex);
        return m_cv.wait_for(lock, timeout, [&]() {
            return m_audioFrames > 0;
        });
    }

    bool waitForAudioFrames(int count, std::chrono::milliseconds timeout = 3s)
    {
        std::unique_lock lock(m_mutex);
        return m_cv.wait_for(lock, timeout, [&]() {
            return m_audioFrames >= count;
        });
    }

    void blockVideoFrames()
    {
        std::scoped_lock lock(m_mutex);
        m_blockVideoFrames = true;
        m_videoFrameBlocked = false;
    }

    bool waitForBlockedVideoFrame(std::chrono::milliseconds timeout = 3s)
    {
        std::unique_lock lock(m_mutex);
        return m_cv.wait_for(lock, timeout, [&]() {
            return m_videoFrameBlocked;
        });
    }

    void releaseVideoFrames()
    {
        {
            std::scoped_lock lock(m_mutex);
            m_blockVideoFrames = false;
        }
        m_cv.notify_all();
    }

    bool waitForVideoFrame(std::chrono::milliseconds timeout = 3s)
    {
        std::unique_lock lock(m_mutex);
        return m_cv.wait_for(lock, timeout, [&]() {
            return m_videoFrames > 0;
        });
    }

    void blockAudioFrames()
    {
        std::scoped_lock lock(m_mutex);
        m_blockAudioFrames = true;
        m_audioFrameBlocked = false;
    }

    bool waitForBlockedAudioFrame(std::chrono::milliseconds timeout = 3s)
    {
        std::unique_lock lock(m_mutex);
        return m_cv.wait_for(lock, timeout, [&]() {
            return m_audioFrameBlocked;
        });
    }

    void releaseAudioFrames()
    {
        {
            std::scoped_lock lock(m_mutex);
            m_blockAudioFrames = false;
        }
        m_cv.notify_all();
    }

    void cancelBlockedAudioFrames()
    {
        {
            std::scoped_lock lock(m_mutex);
            m_cancelAudioFrames = true;
            m_blockAudioFrames = false;
        }
        m_cv.notify_all();
    }

    int audioFrames() const
    {
        std::scoped_lock lock(m_mutex);
        return m_audioFrames;
    }

    int videoFrames() const
    {
        std::scoped_lock lock(m_mutex);
        return m_videoFrames;
    }

    std::chrono::microseconds lastAudioPts() const
    {
        std::scoped_lock lock(m_mutex);
        return m_lastAudioPts;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    int m_audioFrames = 0;
    int m_videoFrames = 0;
    std::uint64_t m_lastSessionId = 0;
    std::uint64_t m_lastGeneration = 0;
    std::chrono::microseconds m_lastAudioPts { 0 };
    std::chrono::microseconds m_lastVideoPts { 0 };
    bool m_blockAudioFrames = false;
    bool m_audioFrameBlocked = false;
    bool m_cancelAudioFrames = false;
    bool m_blockVideoFrames = false;
    bool m_videoFrameBlocked = false;
};

bool hasState(const media_sdk::PlayerEvent& event, media_sdk::PlayerState state)
{
    if (const auto* payload = std::get_if<media_sdk::StateChangedEvent>(&event.payload))
        return payload->state == state;
    return false;
}

bool hasMediaInfo(const media_sdk::PlayerEvent& event)
{
    return std::holds_alternative<media_sdk::MediaInfoEvent>(event.payload);
}

bool hasEof(const media_sdk::PlayerEvent& event)
{
    return std::holds_alternative<media_sdk::EndOfFileEvent>(event.payload);
}

bool hasError(const media_sdk::PlayerEvent& event)
{
    return std::holds_alternative<media_sdk::ErrorEvent>(event.payload);
}

bool hasDecodePerformance(const media_sdk::PlayerEvent& event)
{
    return std::holds_alternative<media_sdk::DecodePerformanceEvent>(event.payload);
}

bool hasPositionAtOrAfter(const media_sdk::PlayerEvent& event,
                          std::chrono::milliseconds position)
{
    if (const auto* payload = std::get_if<media_sdk::PositionChangedEvent>(&event.payload))
        return payload->position >= position;
    return false;
}

bool hasPositionChanged(const media_sdk::PlayerEvent& event)
{
    return std::holds_alternative<media_sdk::PositionChangedEvent>(event.payload);
}

bool hasSeekCompletedAtOrAfter(const media_sdk::PlayerEvent& event,
                               std::chrono::milliseconds position)
{
    if (const auto* payload = std::get_if<media_sdk::SeekCompletedEvent>(&event.payload))
        return payload->position >= position;
    return false;
}

const media_sdk::PlayerEvent* firstEventMatching(
    const std::vector<media_sdk::PlayerEvent>& events,
    bool (*predicate)(const media_sdk::PlayerEvent&))
{
    const auto it = std::ranges::find_if(events, predicate);
    return it == events.end() ? nullptr : &(*it);
}

void assertSingleSession(const std::vector<media_sdk::PlayerEvent>& events,
                         std::uint64_t expectedSessionId)
{
    assert(expectedSessionId > 0);
    for (const auto& event : events)
        assert(event.metadata.sessionId == expectedSessionId);
}

void testOpenPlayReachesEof()
{
    const auto samplePath = writeTinyWav();
    RecordingSink sink;
    RecordingFrameSink frames;
    media_sdk::Player player({}, sink, frames);

    assert(player.open(samplePath).ok());
    assert(sink.waitFor(hasMediaInfo));
    player.play();

    assert(frames.waitForAudioFrame());
    assert(sink.waitFor(hasEof));
    assert(sink.waitFor([](const media_sdk::PlayerEvent& event) {
        return hasState(event, media_sdk::PlayerState::Finished);
    }));

    const auto events = sink.snapshot();
    const auto* mediaInfo = firstEventMatching(events, hasMediaInfo);
    assert(mediaInfo);
    assert(mediaInfo->metadata.sessionId > 0);
    assert(mediaInfo->metadata.generation == 0);
    assertSingleSession(events, mediaInfo->metadata.sessionId);

    const auto* eof = firstEventMatching(events, hasEof);
    assert(eof);
    assert(eof->metadata.sessionId == mediaInfo->metadata.sessionId);
    assert(eof->metadata.generation == mediaInfo->metadata.generation);

    std::filesystem::remove(samplePath);
}

void testSeekEmitsPositionAndContinuesPlayback()
{
    const auto samplePath = writeTinyWav();
    RecordingSink sink;
    RecordingFrameSink frames;
    media_sdk::Player player({}, sink, frames);

    assert(player.open(samplePath).ok());
    assert(sink.waitFor(hasMediaInfo));
    player.play();
    const auto beforeSeekEvents = sink.snapshot();
    const auto* mediaInfoBeforeSeek = firstEventMatching(beforeSeekEvents, hasMediaInfo);
    assert(mediaInfoBeforeSeek);
    const std::uint64_t openGeneration = mediaInfoBeforeSeek->metadata.generation;

    assert(player.seek(100ms).ok());

    assert(sink.waitFor([](const media_sdk::PlayerEvent& event) {
        return hasSeekCompletedAtOrAfter(event, 100ms);
    }));
    assert(sink.waitFor([](const media_sdk::PlayerEvent& event) {
        return hasPositionAtOrAfter(event, 100ms);
    }));
    assert(frames.waitForAudioFrame());

    const auto events = sink.snapshot();
    const auto* mediaInfo = firstEventMatching(events, hasMediaInfo);
    assert(mediaInfo);
    assert(mediaInfo->metadata.sessionId > 0);
    assert(mediaInfo->metadata.generation == 0);
    assertSingleSession(events, mediaInfo->metadata.sessionId);

    const auto positionAfterSeek = std::ranges::find_if(events, [](const media_sdk::PlayerEvent& event) {
        return hasPositionAtOrAfter(event, 100ms);
    });
    assert(positionAfterSeek != events.end());
    assert(positionAfterSeek->metadata.sessionId == mediaInfo->metadata.sessionId);
    assert(positionAfterSeek->metadata.generation > mediaInfo->metadata.generation);

    const auto seekCompleted = std::ranges::find_if(events, [](const media_sdk::PlayerEvent& event) {
        return hasSeekCompletedAtOrAfter(event, 100ms);
    });
    assert(seekCompleted != events.end());
    assert(seekCompleted->metadata.sessionId == mediaInfo->metadata.sessionId);
    assert(seekCompleted->metadata.generation > openGeneration);
    const auto* seekCompletedPayload =
        std::get_if<media_sdk::SeekCompletedEvent>(&seekCompleted->payload);
    assert(seekCompletedPayload);
    assert(seekCompletedPayload->requestedPosition == seekCompletedPayload->position);

    player.stop();
    std::filesystem::remove(samplePath);
}

void testPausedSeekPrerollsFrameWithoutResumingPlayback()
{
    const auto samplePath = writeTinyWav();
    RecordingSink sink;
    RecordingFrameSink frames;
    media_sdk::Player player({}, sink, frames);

    assert(player.open(samplePath).ok());
    assert(sink.waitFor(hasMediaInfo));

    assert(player.seek(100ms).ok());

    assert(sink.waitFor([](const media_sdk::PlayerEvent& event) {
        return hasSeekCompletedAtOrAfter(event, 100ms);
    }));
    assert(frames.waitForAudioFrame());

    const auto events = sink.snapshot();
    assert(std::ranges::none_of(events, [](const media_sdk::PlayerEvent& event) {
        return hasState(event, media_sdk::PlayerState::Playing);
    }));
    assert(!sink.waitFor(hasEof, 100ms));

    player.stop();
    std::filesystem::remove(samplePath);
}

void testSeekCompletionEmitsWhenTargetFrameReachesDeliveryBoundary()
{
    const auto samplePath = writeTinyWav();
    RecordingSink sink;
    RecordingFrameSink frames;
    media_sdk::Player player({}, sink, frames);

    assert(player.open(samplePath).ok());
    assert(sink.waitFor(hasMediaInfo));

    frames.blockAudioFrames();
    assert(player.seek(100ms).ok());
    assert(frames.waitForBlockedAudioFrame());

    assert(sink.waitFor([](const media_sdk::PlayerEvent& event) {
        return hasSeekCompletedAtOrAfter(event, 100ms);
    }, 100ms));

    frames.releaseAudioFrames();
    assert(frames.waitForAudioFrame());

    player.stop();
    std::filesystem::remove(samplePath);
}

void testPausedSeekDoesNotPrerollVideoBeforeTarget()
{
    const auto samplePath = writeLongGopVideoSample();
    RecordingSink sink;
    RecordingFrameSink frames;
    media_sdk::Player player({}, sink, frames);

    assert(player.open(samplePath).ok());
    assert(sink.waitFor(hasMediaInfo));

    assert(player.seek(100ms).ok());
    assert(!frames.waitForVideoFrame(200ms));
    assert(sink.waitFor([](const media_sdk::PlayerEvent& event) {
        return hasSeekCompletedAtOrAfter(event, 100ms);
    }, 500ms));

    player.stop();
    std::filesystem::remove(samplePath);
}

void testSeekDoesNotPushAudioBeforeTarget()
{
    const auto samplePath = writeTinyWav();
    RecordingSink sink;
    RecordingFrameSink frames;
    media_sdk::Player player({}, sink, frames);

    assert(player.open(samplePath).ok());
    assert(sink.waitFor(hasMediaInfo));

    assert(player.seek(100ms).ok());
    assert(frames.waitForAudioFrame());

    assert(frames.lastAudioPts() >= 100ms);

    player.stop();
    std::filesystem::remove(samplePath);
}

void testSeekEmitsAudioPositionWhenTargetFrameReachesDeliveryBoundary()
{
    const auto samplePath = writeTinyWav();
    RecordingSink sink;
    RecordingFrameSink frames;
    media_sdk::Player player({}, sink, frames);

    assert(player.open(samplePath).ok());
    assert(sink.waitFor(hasMediaInfo));

    frames.blockAudioFrames();
    assert(player.seek(100ms).ok());
    assert(frames.waitForBlockedAudioFrame());
    assert(sink.waitFor(hasPositionChanged, 100ms));

    frames.releaseAudioFrames();
    assert(sink.waitFor([](const media_sdk::PlayerEvent& event) {
        return hasSeekCompletedAtOrAfter(event, 100ms);
    }));

    player.stop();
    std::filesystem::remove(samplePath);
}

void testPausedVideoSeekPrerollSkipsAudioBackpressure()
{
    const auto samplePath = writeAudioFirstVideoSample();
    RecordingSink sink;
    RecordingFrameSink frames;
    media_sdk::Player player({}, sink, frames);

    assert(player.open(samplePath).ok());
    assert(sink.waitFor(hasMediaInfo));

    frames.blockAudioFrames();
    assert(player.seek(0ms).ok());

    assert(sink.waitFor([](const media_sdk::PlayerEvent& event) {
        return hasSeekCompletedAtOrAfter(event, 0ms);
    }));
    assert(frames.waitForVideoFrame(500ms));
    assert(!frames.waitForBlockedAudioFrame(100ms));

    frames.releaseAudioFrames();
    player.stop();
    std::filesystem::remove(samplePath);
}

void testPlayingSeekDiscardLimitKeepsFilteringUntilTargetAudio()
{
    const auto samplePath = writeTinyWav();
    RecordingSink sink;
    RecordingFrameSink frames;
    media_sdk::PlayerConfig config;
    config.accurateSeekMaxDiscardedAudioFrames = 1;
    media_sdk::Player player(config, sink, frames);

    assert(player.open(samplePath).ok());
    assert(sink.waitFor(hasMediaInfo));

    frames.blockAudioFrames();
    player.play();
    assert(frames.waitForBlockedAudioFrame());

    assert(player.seek(300ms).ok());
    frames.releaseAudioFrames();

    assert(sink.waitFor([](const media_sdk::PlayerEvent& event) {
        return hasSeekCompletedAtOrAfter(event, 300ms);
    }));
    assert(frames.waitForAudioFrames(2));
    assert(frames.lastAudioPts() >= 300ms);

    player.stop();
    std::filesystem::remove(samplePath);
}

void testBurstSeekCoalescesQueuedRequestsBeforeDecodeResumes()
{
    const auto samplePath = writeTinyWav();
    RecordingSink sink;
    RecordingFrameSink frames;
    media_sdk::Player player({}, sink, frames);

    assert(player.open(samplePath).ok());
    assert(sink.waitFor(hasMediaInfo));

    frames.blockAudioFrames();
    player.play();
    assert(frames.waitForBlockedAudioFrame());

    assert(player.seek(100ms, media_sdk::SeekPlaybackMode::PreservePlaybackState, 101).ok());
    assert(player.seek(150ms, media_sdk::SeekPlaybackMode::PreservePlaybackState, 102).ok());
    assert(player.seek(200ms, media_sdk::SeekPlaybackMode::PreservePlaybackState, 103).ok());

    frames.releaseAudioFrames();

    assert(sink.waitFor([](const media_sdk::PlayerEvent& event) {
        return hasSeekCompletedAtOrAfter(event, 200ms);
    }));

    player.stop();

    const auto events = sink.snapshot();
    int seekCompletedCount = 0;
    std::chrono::milliseconds lastSeekPosition { 0 };
    for (const auto& event : events)
    {
        if (const auto* payload = std::get_if<media_sdk::SeekCompletedEvent>(&event.payload))
        {
            ++seekCompletedCount;
            lastSeekPosition = payload->position;
        }
    }

    assert(seekCompletedCount == 1);
    assert(lastSeekPosition == 200ms);
    assert(frames.lastAudioPts() >= 200ms);
    const auto* seekCompleted = firstEventMatching(events, [](const media_sdk::PlayerEvent& event) {
        return std::holds_alternative<media_sdk::SeekCompletedEvent>(event.payload);
    });
    assert(seekCompleted);
    const auto* payload = std::get_if<media_sdk::SeekCompletedEvent>(&seekCompleted->payload);
    assert(payload);
    assert(payload->requestedPosition == payload->position);
    assert(payload->requestId == 103);

    std::filesystem::remove(samplePath);
}

void testStopEmitsStoppedState()
{
    const auto samplePath = writeTinyWav();
    RecordingSink sink;
    RecordingFrameSink frames;
    media_sdk::Player player({}, sink, frames);

    assert(player.open(samplePath).ok());
    assert(sink.waitFor(hasMediaInfo));
    player.play();
    player.stop();

    assert(sink.waitFor([](const media_sdk::PlayerEvent& event) {
        return hasState(event, media_sdk::PlayerState::Stopped);
    }));

    std::filesystem::remove(samplePath);
}

void testDecodeWorkerPublishesPerformanceReportsWithoutPerFramePoolMetadata()
{
    const auto samplePath = writeTinyWav();
    RecordingSink sink;
    RecordingFrameSink frames;
    media_sdk::PlayerConfig config;
    config.decodePerformanceReportInterval = 0ms;
    media_sdk::Player player(config, sink, frames);

    assert(player.open(samplePath).ok());
    assert(sink.waitFor(hasMediaInfo));
    player.play();
    assert(sink.waitFor(hasDecodePerformance));

    const auto events = sink.snapshot();
    const auto* event = firstEventMatching(events, hasDecodePerformance);
    assert(event);
    const auto* report = std::get_if<media_sdk::DecodePerformanceEvent>(&event->payload);
    assert(report);
    assert(report->videoPicturePool.acquireCount == 0);
    assert(report->videoPicturePool.inFlightCount == 0);

    player.stop();
    std::filesystem::remove(samplePath);
}

void testCancelledFramePushDuringStopDoesNotEmitDecodeError()
{
    const auto samplePath = writeTinyWav();
    RecordingSink sink;
    RecordingFrameSink frames;
    media_sdk::Player player({}, sink, frames);

    assert(player.open(samplePath).ok());
    assert(sink.waitFor(hasMediaInfo));

    frames.blockAudioFrames();
    player.play();
    assert(frames.waitForBlockedAudioFrame());

    frames.cancelBlockedAudioFrames();
    player.stop();

    assert(sink.waitFor([](const media_sdk::PlayerEvent& event) {
        return hasState(event, media_sdk::PlayerState::Stopped);
    }));

    const auto events = sink.snapshot();
    assert(std::ranges::none_of(events, hasError));

    std::filesystem::remove(samplePath);
}

} // namespace

int main()
{
    testOpenPlayReachesEof();
    testSeekEmitsPositionAndContinuesPlayback();
    testPausedSeekPrerollsFrameWithoutResumingPlayback();
    testSeekCompletionEmitsWhenTargetFrameReachesDeliveryBoundary();
    testPausedSeekDoesNotPrerollVideoBeforeTarget();
    testSeekDoesNotPushAudioBeforeTarget();
    testSeekEmitsAudioPositionWhenTargetFrameReachesDeliveryBoundary();
    testPausedVideoSeekPrerollSkipsAudioBackpressure();
    testPlayingSeekDiscardLimitKeepsFilteringUntilTargetAudio();
    testBurstSeekCoalescesQueuedRequestsBeforeDecodeResumes();
    testStopEmitsStoppedState();
    testDecodeWorkerPublishesPerformanceReportsWithoutPerFramePoolMetadata();
    testCancelledFramePushDuringStopDoesNotEmitDecodeError();
    return 0;
}
