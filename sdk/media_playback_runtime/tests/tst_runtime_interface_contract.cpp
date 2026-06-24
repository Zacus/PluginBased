#include "media_sdk/runtime/AudioOutput.h"
#include "media_sdk/runtime/RuntimeTypes.h"
#include "media_sdk/runtime/VideoPresenter.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace {

class MockAudioOutput final : public media_sdk::runtime::IAudioOutput {
public:
    media_sdk::Result<void> open(const media_sdk::runtime::AudioFormat& format) override
    {
        m_format = format;
        return media_sdk::Result<void>::success();
    }

    media_sdk::Result<void> write(media_sdk::runtime::AudioBufferView buffer) override
    {
        m_written += buffer.bytes.size();
        return media_sdk::Result<void>::success();
    }

    media_sdk::runtime::ClockSnapshot clock() const override
    {
        return media_sdk::runtime::ClockSnapshot {
            .position = 42ms,
            .hardwareLatency = 3ms,
            .queuedDuration = 8ms,
            .generation = 7,
            .valid = true,
            .paused = false,
        };
    }

    void pause() override { m_paused = true; }
    void resume() override { m_paused = false; }
    void flush() override { ++m_flushCount; }
    void close() override { m_closed = true; }

    media_sdk::runtime::AudioFormat m_format {};
    std::size_t m_written = 0;
    int m_flushCount = 0;
    bool m_paused = false;
    bool m_closed = false;
};

class MockPresenter final : public media_sdk::runtime::IVideoPresenter {
public:
    media_sdk::runtime::VideoPresenterCapabilities capabilities() const override
    {
        return {
            .supportsVideoToolboxPixelBuffer = true,
            .supportsCpuYuv = true,
            .asyncPresent = true,
        };
    }

    void setEvents(media_sdk::runtime::IVideoPresenterEvents* events) override
    {
        m_events = events;
    }

    media_sdk::runtime::PresentResult present(
        media_sdk::VideoFrame frame,
        media_sdk::runtime::PresentTiming timing) override
    {
        m_lastPts = timing.pts;
        m_lastFrame = std::move(frame);
        return {
            .id = ++m_nextId,
            .status = media_sdk::runtime::PresentStatus::Queued,
        };
    }

    void clear() override
    {
        m_cleared = true;
    }

    media_sdk::runtime::IVideoPresenterEvents* m_events = nullptr;
    media_sdk::VideoFrame m_lastFrame {};
    std::chrono::microseconds m_lastPts { 0 };
    media_sdk::runtime::PresentId m_nextId = 0;
    bool m_cleared = false;
};

} // namespace

int main()
{
    MockAudioOutput audio;
    const auto openResult = audio.open({ 48000, 2, media_sdk::runtime::AudioSampleFormat::Float32Planar });
    assert(openResult.ok());
    assert(audio.clock().valid);
    assert(audio.clock().generation == 7);

    std::vector<std::byte> bytes(128);
    const auto writeResult = audio.write({ bytes, 1000us, 7 });
    assert(writeResult.ok());
    assert(audio.m_written == bytes.size());

    MockPresenter presenter;
    assert(presenter.capabilities().supportsVideoToolboxPixelBuffer);
    const auto result = presenter.present({}, { 1000us, 995us, 5us });
    assert(result.id == 1);
    assert(result.status == media_sdk::runtime::PresentStatus::Queued);

    media_sdk::runtime::RuntimeVideoFrame runtimeFrame;
    runtimeFrame.sessionId = 11;
    runtimeFrame.generation = 22;
    assert(runtimeFrame.sessionId == 11);
    assert(runtimeFrame.generation == 22);
}
