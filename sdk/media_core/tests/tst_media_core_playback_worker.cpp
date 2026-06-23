#include "media_sdk/Player.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

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

bool hasAudioFrame(const media_sdk::PlayerEvent& event)
{
    return std::holds_alternative<media_sdk::AudioFrameEvent>(event.payload);
}

bool hasEof(const media_sdk::PlayerEvent& event)
{
    return std::holds_alternative<media_sdk::EndOfFileEvent>(event.payload);
}

bool hasPositionAtOrAfter(const media_sdk::PlayerEvent& event,
                          std::chrono::milliseconds position)
{
    if (const auto* payload = std::get_if<media_sdk::PositionChangedEvent>(&event.payload))
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
    media_sdk::Player player({}, sink);

    assert(player.open(samplePath).ok());
    assert(sink.waitFor(hasMediaInfo));
    player.play();

    assert(sink.waitFor(hasAudioFrame));
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
    media_sdk::Player player({}, sink);

    assert(player.open(samplePath).ok());
    assert(sink.waitFor(hasMediaInfo));
    player.play();
    assert(player.seek(100ms).ok());

    assert(sink.waitFor([](const media_sdk::PlayerEvent& event) {
        return hasPositionAtOrAfter(event, 100ms);
    }));
    assert(sink.waitFor(hasAudioFrame));

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

    player.stop();
    std::filesystem::remove(samplePath);
}

void testStopEmitsStoppedState()
{
    const auto samplePath = writeTinyWav();
    RecordingSink sink;
    media_sdk::Player player({}, sink);

    assert(player.open(samplePath).ok());
    assert(sink.waitFor(hasMediaInfo));
    player.play();
    player.stop();

    assert(sink.waitFor([](const media_sdk::PlayerEvent& event) {
        return hasState(event, media_sdk::PlayerState::Stopped);
    }));

    std::filesystem::remove(samplePath);
}

} // namespace

int main()
{
    testOpenPlayReachesEof();
    testSeekEmitsPositionAndContinuesPlayback();
    testStopEmitsStoppedState();
    return 0;
}
