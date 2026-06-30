#include "CoreAudioOutputEngine.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace {

struct FakeRenderDeviceStats {
    int openCount = 0;
    int startCount = 0;
    int stopCount = 0;
    int resetCount = 0;
    int closeCount = 0;
};

class FakeRenderDevice final : public media_sdk::platform::macos::IAudioRenderDevice {
public:
    explicit FakeRenderDevice(std::shared_ptr<FakeRenderDeviceStats> stats)
        : stats(std::move(stats))
    {
    }

    media_sdk::Result<void> open(
        const media_sdk::platform::macos::AudioRenderDeviceConfig& config) override
    {
        ++stats->openCount;
        callback = config.callback;
        format = config.format;
        openState = true;
        return media_sdk::Result<void>::success();
    }

    media_sdk::Result<void> start() override
    {
        ++stats->startCount;
        running = true;
        return media_sdk::Result<void>::success();
    }

    void stop() noexcept override
    {
        ++stats->stopCount;
        running = false;
    }

    void reset() noexcept override
    {
        ++stats->resetCount;
    }

    void close() noexcept override
    {
        if (!openState && !running)
            return;

        ++stats->closeCount;
        openState = false;
        running = false;
    }

    std::chrono::microseconds hardwareLatency() const noexcept override
    {
        return 2000us;
    }

    media_sdk::platform::macos::AudioRenderDeviceDiagnostics diagnostics() const noexcept override
    {
        return diagnosticsValue;
    }

    void render(std::span<std::byte> destination)
    {
        if (callback.function)
            callback.function(callback.context, destination);
    }

    std::shared_ptr<FakeRenderDeviceStats> stats;
    media_sdk::platform::macos::AudioRenderCallback callback {};
    media_sdk::platform::macos::AudioRenderDeviceDiagnostics diagnosticsValue {};
    media_sdk::runtime::AudioFormat format {};
    bool openState = false;
    bool running = false;
};

media_sdk::runtime::AudioFormat audioFormat()
{
    return {
        .sampleRate = 48000,
        .channels = 2,
        .sampleFormat = media_sdk::runtime::AudioSampleFormat::Float32,
    };
}

std::vector<std::byte> bytes(std::initializer_list<unsigned int> values)
{
    std::vector<std::byte> result;
    result.reserve(values.size());
    for (const auto value : values)
        result.push_back(static_cast<std::byte>(value));
    return result;
}

struct EngineFixture {
    std::unique_ptr<media_sdk::platform::macos::CoreAudioOutputEngine> engine;
    FakeRenderDevice* device = nullptr;
    std::shared_ptr<FakeRenderDeviceStats> stats;
};

EngineFixture makeEngine()
{
    auto stats = std::make_shared<FakeRenderDeviceStats>();
    auto device = std::make_unique<FakeRenderDevice>(stats);
    auto* rawDevice = device.get();
    return {
        .engine = std::make_unique<media_sdk::platform::macos::CoreAudioOutputEngine>(
            std::move(device)),
        .device = rawDevice,
        .stats = stats,
    };
}

void resumeStartsDeviceAndCallbackConsumesPcm()
{
    auto [engine, device, stats] = makeEngine();
    assert(engine->open(audioFormat()).ok());

    const auto input = bytes({ 1, 2, 3, 4, 5, 6, 7, 8 });
    assert(engine->write({
        .bytes = input,
        .pts = 10ms,
        .generation = 1,
    }).ok());

    engine->resume();
    assert(stats->startCount == 1);
    assert(device->running);

    std::vector<std::byte> output(8);
    device->render(output);
    assert(output == input);
    assert(engine->clock().position == 10ms + 20us);
}

void pauseStopsDeviceWithoutFlushingQueuedAudio()
{
    auto [engine, device, stats] = makeEngine();
    assert(engine->open(audioFormat()).ok());

    const auto input = bytes({ 9, 8, 7, 6, 5, 4, 3, 2 });
    assert(engine->write({
        .bytes = input,
        .pts = 20ms,
        .generation = 1,
    }).ok());

    engine->resume();
    engine->pause();
    assert(stats->stopCount == 1);
    assert(engine->clock().paused);

    std::vector<std::byte> output(8);
    device->render(output);
    assert(output == input);
}

void flushResetsDeviceAndRejectsOldGeneration()
{
    auto [engine, device, stats] = makeEngine();
    assert(engine->open(audioFormat()).ok());

    const auto input = bytes({ 1, 1, 1, 1, 2, 2, 2, 2 });
    assert(engine->write({
        .bytes = input,
        .pts = 30ms,
        .generation = 1,
    }).ok());

    engine->flush();
    assert(stats->resetCount == 1);
    assert(engine->clock().generation == 2);
    assert(!engine->write({
        .bytes = input,
        .pts = 40ms,
        .generation = 1,
    }).ok());
    assert(engine->write({
        .bytes = input,
        .pts = 40ms,
        .generation = 2,
    }).ok());
}

void closeStopsAndRejectsWrites()
{
    auto [engine, device, stats] = makeEngine();
    assert(engine->open(audioFormat()).ok());
    engine->resume();
    engine->close();

    assert(stats->stopCount == 1);
    assert(stats->closeCount == 1);
    assert(!engine->clock().valid);

    const auto input = bytes({ 4, 3, 2, 1, 0, 1, 2, 3 });
    assert(!engine->write({
        .bytes = input,
        .pts = 50ms,
        .generation = 1,
    }).ok());
}

void destructorStopsAndClosesDevice()
{
    std::shared_ptr<FakeRenderDeviceStats> stats;
    {
        auto fixture = makeEngine();
        stats = fixture.stats;
        assert(fixture.engine->open(audioFormat()).ok());
        fixture.engine->resume();
    }

    assert(stats->stopCount == 1);
    assert(stats->closeCount == 1);
}

} // namespace

int main()
{
    resumeStartsDeviceAndCallbackConsumesPcm();
    pauseStopsDeviceWithoutFlushingQueuedAudio();
    flushResetsDeviceAndRejectsOldGeneration();
    closeStopsAndRejectsWrites();
    destructorStopsAndClosesDevice();
}
