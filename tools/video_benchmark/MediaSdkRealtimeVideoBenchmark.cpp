#include "media_sdk/session/PlaybackSession.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <thread>

namespace {

using namespace std::chrono_literals;

struct Options {
    std::filesystem::path input;
    std::filesystem::path output;
    std::string label = "unlabelled";
    std::uint64_t maxPresentedFrames = 600;
    std::chrono::milliseconds timeout { 30'000 };
};

std::string jsonEscape(std::string_view value)
{
    std::ostringstream escaped;
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': escaped << "\\\""; break;
        case '\\': escaped << "\\\\"; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default: escaped << static_cast<char>(ch); break;
        }
    }
    return escaped.str();
}

std::optional<std::string> nextValue(int& index, int argc, char** argv)
{
    if (index + 1 >= argc)
        return std::nullopt;
    return std::string(argv[++index]);
}

std::optional<Options> parseOptions(int argc, char** argv)
{
    Options options;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument(argv[i]);
            if (argument == "--input") {
                const auto value = nextValue(i, argc, argv);
                if (!value) return std::nullopt;
                options.input = *value;
            } else if (argument == "--output") {
                const auto value = nextValue(i, argc, argv);
                if (!value) return std::nullopt;
                options.output = *value;
            } else if (argument == "--label") {
                const auto value = nextValue(i, argc, argv);
                if (!value) return std::nullopt;
                options.label = *value;
            } else if (argument == "--max-presented-frames") {
                const auto value = nextValue(i, argc, argv);
                if (!value) return std::nullopt;
                options.maxPresentedFrames = std::stoull(*value);
            } else if (argument == "--timeout-ms") {
                const auto value = nextValue(i, argc, argv);
                if (!value) return std::nullopt;
                options.timeout = std::chrono::milliseconds(std::stoll(*value));
            } else {
                return std::nullopt;
            }
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (options.input.empty() || options.output.empty() || options.maxPresentedFrames == 0)
        return std::nullopt;
    return options;
}

double timevalSeconds(const timeval& value)
{
    return static_cast<double>(value.tv_sec) + static_cast<double>(value.tv_usec) / 1'000'000.0;
}

std::uint64_t maxRssBytes(const rusage& usage)
{
#if defined(__APPLE__)
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ULL;
#endif
}

std::string systemName()
{
    utsname info {};
    if (uname(&info) != 0)
        return "unknown";
    return std::string(info.sysname) + " " + info.release + " " + info.machine;
}

class RealtimeAudioOutput final : public media_sdk::runtime::IAudioOutput {
public:
    media_sdk::Result<void> open(const media_sdk::runtime::AudioFormat& format) override
    {
        std::lock_guard lock(m_mutex);
        m_format = format;
        m_open = true;
        m_paused = true;
        m_anchor.reset();
        return media_sdk::Result<void>::success();
    }

    media_sdk::Result<void> write(media_sdk::runtime::AudioBufferView buffer) override
    {
        std::unique_lock lock(m_mutex);
        if (!m_open)
            return media_sdk::Result<void>::failure({
                media_sdk::MediaErrorCode::InternalStateError,
                "Realtime benchmark audio output is closed",
                {},
            });

        if (!m_anchor.has_value()) {
            m_anchor = Anchor {
                .media = buffer.pts,
                .wall = std::chrono::steady_clock::now(),
                .generation = buffer.generation,
            };
        }
        const auto target = m_anchor->wall + (buffer.pts - m_anchor->media);
        m_changed.wait_until(lock, target, [this]() { return !m_open || m_paused; });
        return media_sdk::Result<void>::success();
    }

    media_sdk::runtime::ClockSnapshot clock() const override
    {
        std::lock_guard lock(m_mutex);
        if (!m_open || !m_anchor.has_value())
            return {};
        const auto elapsed = m_paused
            ? m_pausedAt - m_anchor->wall
            : std::chrono::steady_clock::now() - m_anchor->wall;
        return {
            .position = m_anchor->media
                + std::chrono::duration_cast<std::chrono::microseconds>(elapsed),
            .generation = m_anchor->generation,
            .valid = true,
            .paused = m_paused,
        };
    }

    void pause() override
    {
        std::lock_guard lock(m_mutex);
        if (!m_paused)
            m_pausedAt = std::chrono::steady_clock::now();
        m_paused = true;
        m_changed.notify_all();
    }

    media_sdk::Result<void> resume() override
    {
        std::lock_guard lock(m_mutex);
        if (m_paused && m_anchor.has_value())
            m_anchor->wall += std::chrono::steady_clock::now() - m_pausedAt;
        m_paused = false;
        m_changed.notify_all();
        return media_sdk::Result<void>::success();
    }

    media_sdk::Result<void> flush() override
    {
        std::lock_guard lock(m_mutex);
        m_anchor.reset();
        return media_sdk::Result<void>::success();
    }

    void close() override
    {
        std::lock_guard lock(m_mutex);
        m_open = false;
        m_anchor.reset();
        m_changed.notify_all();
    }

private:
    struct Anchor {
        std::chrono::microseconds media { 0 };
        std::chrono::steady_clock::time_point wall;
        media_sdk::runtime::Generation generation = 0;
    };

    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    media_sdk::runtime::AudioFormat m_format;
    std::optional<Anchor> m_anchor;
    std::chrono::steady_clock::time_point m_pausedAt = std::chrono::steady_clock::now();
    bool m_open = false;
    bool m_paused = true;
};

class BenchmarkPresenter final : public media_sdk::runtime::IVideoPresenter {
public:
    explicit BenchmarkPresenter(std::uint64_t maxFrames)
        : m_maxFrames(maxFrames)
    {
    }

    media_sdk::runtime::VideoPresenterCapabilities capabilities() const override
    {
        return {
            .supportsVideoToolboxPixelBuffer = false,
            .supportsCpuYuv = true,
            .asyncPresent = false,
            .maxPendingFrames = 1,
        };
    }

    void setEvents(media_sdk::runtime::IVideoPresenterEvents* events) override
    {
        std::lock_guard lock(m_mutex);
        m_events = events;
    }

    media_sdk::runtime::PresentResult present(
        media_sdk::VideoFrame frame,
        media_sdk::runtime::PresentTiming timing) override
    {
        std::lock_guard lock(m_mutex);
        if (m_presentedFrames >= m_maxFrames)
            return { .status = media_sdk::runtime::PresentStatus::Skipped };

        ++m_presentedFrames;
        m_latenessTotalUs += timing.lateness.count();
        m_latenessMaxUs = std::max(m_latenessMaxUs, timing.lateness.count());
        for (const auto& plane : frame.planes()) {
            if (!plane.data)
                continue;
            m_checksum ^= static_cast<std::uint64_t>(
                *reinterpret_cast<const std::uint8_t*>(plane.data));
            m_checksum *= 1'099'511'628'211ULL;
        }
        if (m_presentedFrames == m_maxFrames)
            m_changed.notify_all();
        return { .status = media_sdk::runtime::PresentStatus::Presented };
    }

    void clear() override {}

    bool waitForTarget(std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, timeout, [this]() {
            return m_presentedFrames >= m_maxFrames;
        });
    }

    struct Snapshot {
        std::uint64_t presentedFrames = 0;
        std::uint64_t checksum = 0;
        std::int64_t latenessTotalUs = 0;
        std::int64_t latenessMaxUs = 0;
    };

    Snapshot snapshot() const
    {
        std::lock_guard lock(m_mutex);
        return {
            .presentedFrames = m_presentedFrames,
            .checksum = m_checksum,
            .latenessTotalUs = m_latenessTotalUs,
            .latenessMaxUs = m_latenessMaxUs,
        };
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    media_sdk::runtime::IVideoPresenterEvents* m_events = nullptr;
    std::uint64_t m_maxFrames = 0;
    std::uint64_t m_presentedFrames = 0;
    std::uint64_t m_checksum = 1'469'598'103'934'665'603ULL;
    std::int64_t m_latenessTotalUs = 0;
    std::int64_t m_latenessMaxUs = 0;
};

class BenchmarkEvents final : public media_sdk::session::ISessionEvents {
public:
    void onEvent(const media_sdk::PlayerEvent& event) override
    {
        std::lock_guard lock(m_mutex);
        if (const auto* info = std::get_if<media_sdk::MediaInfoEvent>(&event.payload)) {
            m_mediaInfo = info->info;
            m_ready = true;
        } else if (const auto* error = std::get_if<media_sdk::ErrorEvent>(&event.payload)) {
            m_error = error->error.message;
            m_ready = true;
        }
        m_changed.notify_all();
    }

    void onRuntimeDiagnostics(media_sdk::runtime::RuntimeDiagnostics diagnostics) override
    {
        std::lock_guard lock(m_mutex);
        m_lastDiagnostics = diagnostics;
    }

    bool waitUntilReady(std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, timeout, [this]() { return m_ready; });
    }

    media_sdk::MediaInfo mediaInfo() const
    {
        std::lock_guard lock(m_mutex);
        return m_mediaInfo;
    }

    std::optional<std::string> error() const
    {
        std::lock_guard lock(m_mutex);
        return m_error;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    media_sdk::MediaInfo m_mediaInfo;
    media_sdk::runtime::RuntimeDiagnostics m_lastDiagnostics;
    std::optional<std::string> m_error;
    bool m_ready = false;
};

} // namespace

int main(int argc, char** argv)
{
    const auto options = parseOptions(argc, argv);
    if (!options) {
        std::cerr << "Usage: MediaSdkRealtimeVideoBenchmark --input FILE --output FILE "
                     "--label NAME [--max-presented-frames N] [--timeout-ms N]\n";
        return 2;
    }

    RealtimeAudioOutput audioOutput;
    BenchmarkPresenter presenter(options->maxPresentedFrames);
    BenchmarkEvents events;
    media_sdk::session::PlaybackSessionConfig config;
    config.core.enableHardwareDecode = false;
    config.core.preferNativeVideoFrames = false;
    config.preferNativeVideoFrames = false;
    config.runtime.outputPolicy = media_sdk::runtime::VideoOutputPolicy::CpuOnly;
    config.runtime.videoQueueCapacity = 8;
    config.runtime.audioQueueCapacity = 32;

    media_sdk::session::PlaybackSession session(config, {
        .audioOutput = &audioOutput,
        .videoPresenter = &presenter,
        .events = &events,
    });
    const auto openResult = session.open(options->input);
    if (!openResult.ok()) {
        std::cerr << "Open submission failed: " << openResult.error().message << '\n';
        return 1;
    }
    if (!events.waitUntilReady(10s) || events.error()) {
        std::cerr << "Session did not become ready";
        if (events.error()) std::cerr << ": " << *events.error();
        std::cerr << '\n';
        return 1;
    }

    rusage usageBefore {};
    getrusage(RUSAGE_SELF, &usageBefore);
    const auto started = std::chrono::steady_clock::now();
    session.play();
    const bool completed = presenter.waitForTarget(options->timeout);
    const auto diagnostics = session.diagnostics();
    session.stop();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    rusage usageAfter {};
    getrusage(RUSAGE_SELF, &usageAfter);

    const auto mediaInfo = events.mediaInfo();
    const auto presenterSnapshot = presenter.snapshot();
    const auto wallMs = std::chrono::duration<double, std::milli>(elapsed).count();
    const auto userMs = (timevalSeconds(usageAfter.ru_utime) - timevalSeconds(usageBefore.ru_utime)) * 1000.0;
    const auto systemMs = (timevalSeconds(usageAfter.ru_stime) - timevalSeconds(usageBefore.ru_stime)) * 1000.0;
    const auto latenessAverageUs = presenterSnapshot.presentedFrames > 0
        ? static_cast<double>(presenterSnapshot.latenessTotalUs)
            / static_cast<double>(presenterSnapshot.presentedFrames)
        : 0.0;

    std::filesystem::create_directories(options->output.parent_path());
    std::ofstream output(options->output);
    if (!output) {
        std::cerr << "Cannot open output: " << options->output << '\n';
        return 1;
    }
    output << std::fixed << std::setprecision(3)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"benchmark_kind\": \"realtime_pipeline\",\n"
           << "  \"label\": \"" << jsonEscape(options->label) << "\",\n"
           << "  \"input\": \"" << jsonEscape(std::filesystem::absolute(options->input).string()) << "\",\n"
           << "  \"system\": \"" << jsonEscape(systemName()) << "\",\n"
           << "  \"completed\": " << (completed ? "true" : "false") << ",\n"
           << "  \"media\": {\"duration_ms\":" << mediaInfo.duration.count()
           << ",\"width\":" << mediaInfo.width
           << ",\"height\":" << mediaInfo.height
           << ",\"fps\":" << mediaInfo.fps
           << ",\"format\":\"" << jsonEscape(mediaInfo.formatName) << "\"},\n"
           << "  \"presenter\": {\"presented_frames\":" << presenterSnapshot.presentedFrames
           << ",\"checksum\":" << presenterSnapshot.checksum
           << ",\"lateness_average_us\":" << latenessAverageUs
           << ",\"lateness_max_us\":" << presenterSnapshot.latenessMaxUs << "},\n"
           << "  \"timing\": {\"wall_ms\":" << wallMs
           << ",\"user_cpu_ms\":" << userMs
           << ",\"system_cpu_ms\":" << systemMs
           << ",\"max_rss_bytes\":" << maxRssBytes(usageAfter) << "},\n"
           << "  \"runtime\": {\"video_queued\":" << diagnostics.videoQueued
           << ",\"video_presented\":" << diagnostics.videoPresented
           << ",\"video_dropped_late\":" << diagnostics.videoDroppedLate
           << ",\"video_backpressure_count\":" << diagnostics.videoBackpressureCount
           << ",\"video_queue_high_watermark\":" << diagnostics.videoQueueHighWatermark
           << ",\"audio_backpressure_count\":" << diagnostics.audioBackpressureCount
           << ",\"audio_queue_high_watermark\":" << diagnostics.audioQueueHighWatermark
           << ",\"decode_frame_push_wait_us\":" << diagnostics.decodeFramePushWaitUs << "},\n"
           << "  \"pool\": {";
#if defined(MEDIA_SDK_BENCHMARK_HAS_POOL_DIAGNOSTICS)
    output << "\"acquire_count\":" << diagnostics.videoPicturePoolAcquireCount
           << ",\"reuse_count\":" << diagnostics.videoPicturePoolReuseCount
           << ",\"allocation_count\":" << diagnostics.videoPicturePoolAllocationCount
           << ",\"transient_allocation_count\":" << diagnostics.videoPicturePoolTransientAllocationCount
           << ",\"high_watermark\":" << diagnostics.videoPicturePoolHighWatermark
           << ",\"retained_count\":" << diagnostics.videoPicturePoolRetainedCount
           << ",\"in_flight_count\":" << diagnostics.videoPicturePoolInFlightCount;
#else
    output << "\"acquire_count\":0,\"reuse_count\":0,\"allocation_count\":0,"
              "\"transient_allocation_count\":0,\"high_watermark\":0,"
              "\"retained_count\":0,\"in_flight_count\":0";
#endif
    output << "}\n}\n";

    if (!completed) {
        std::cerr << "Realtime benchmark timed out after presenting "
                  << presenterSnapshot.presentedFrames << " frames\n";
        return 1;
    }
    return 0;
}
