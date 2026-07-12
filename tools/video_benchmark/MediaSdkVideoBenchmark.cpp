#include "media_sdk/Player.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <utility>

namespace {

using namespace std::chrono_literals;

struct Options {
    std::filesystem::path input;
    std::filesystem::path output;
    std::string label = "unlabelled";
    bool hardwareDecode = false;
    std::size_t holdVideoFrames = 3;
    std::uint64_t maxVideoFrames = 0;
    std::chrono::milliseconds timeout { 120'000 };
    std::chrono::milliseconds reportInterval { 100 };
};

struct PoolSnapshot {
    std::uint64_t acquireCount = 0;
    std::uint64_t reuseCount = 0;
    std::uint64_t allocationCount = 0;
    std::uint64_t transientAllocationCount = 0;
    std::uint64_t highWatermark = 0;
    std::uint64_t retainedCount = 0;
    std::uint64_t inFlightCount = 0;
};

std::string jsonEscape(std::string_view value)
{
    std::ostringstream escaped;
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': escaped << "\\\""; break;
        case '\\': escaped << "\\\\"; break;
        case '\b': escaped << "\\b"; break;
        case '\f': escaped << "\\f"; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default:
            if (ch < 0x20)
                escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(ch) << std::dec;
            else
                escaped << static_cast<char>(ch);
        }
    }
    return escaped.str();
}

std::optional<std::string> argumentValue(int& index, int argc, char** argv)
{
    if (index + 1 >= argc)
        return std::nullopt;
    return std::string(argv[++index]);
}

std::optional<Options> parseOptions(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--input") {
            const auto value = argumentValue(i, argc, argv);
            if (!value) return std::nullopt;
            options.input = *value;
        } else if (argument == "--output") {
            const auto value = argumentValue(i, argc, argv);
            if (!value) return std::nullopt;
            options.output = *value;
        } else if (argument == "--label") {
            const auto value = argumentValue(i, argc, argv);
            if (!value) return std::nullopt;
            options.label = *value;
        } else if (argument == "--hold-video-frames") {
            const auto value = argumentValue(i, argc, argv);
            if (!value) return std::nullopt;
            options.holdVideoFrames = std::stoull(*value);
        } else if (argument == "--timeout-ms") {
            const auto value = argumentValue(i, argc, argv);
            if (!value) return std::nullopt;
            options.timeout = std::chrono::milliseconds(std::stoll(*value));
        } else if (argument == "--max-video-frames") {
            const auto value = argumentValue(i, argc, argv);
            if (!value) return std::nullopt;
            options.maxVideoFrames = std::stoull(*value);
        } else if (argument == "--report-interval-ms") {
            const auto value = argumentValue(i, argc, argv);
            if (!value) return std::nullopt;
            options.reportInterval = std::chrono::milliseconds(std::stoll(*value));
        } else if (argument == "--hardware") {
            options.hardwareDecode = true;
        } else if (argument == "--software") {
            options.hardwareDecode = false;
        } else {
            std::cerr << "Unknown argument: " << argument << '\n';
            return std::nullopt;
        }
    }

    if (options.input.empty() || options.output.empty())
        return std::nullopt;
    return options;
}

std::string pixelFormatName(media_sdk::PixelFormat format)
{
    switch (format) {
    case media_sdk::PixelFormat::Yuv420P: return "yuv420p";
    case media_sdk::PixelFormat::Nv12: return "nv12";
    case media_sdk::PixelFormat::P010: return "p010";
    case media_sdk::PixelFormat::Yuv420P10: return "yuv420p10";
    case media_sdk::PixelFormat::Yuv422P10: return "yuv422p10";
    case media_sdk::PixelFormat::Yuv444P10: return "yuv444p10";
    case media_sdk::PixelFormat::Native: return "native";
    case media_sdk::PixelFormat::Unknown: return "unknown";
    }
    return "unknown";
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

class BenchmarkSink final : public media_sdk::IEventSink, public media_sdk::IDecodeFrameSink {
public:
    BenchmarkSink(std::size_t holdVideoFrames, std::uint64_t maxVideoFrames)
        : m_holdVideoFrames(holdVideoFrames)
        , m_maxVideoFrames(maxVideoFrames)
    {
    }

    void onEvent(const media_sdk::PlayerEvent& event) override
    {
        std::lock_guard lock(m_mutex);
        if (const auto* info = std::get_if<media_sdk::MediaInfoEvent>(&event.payload))
            m_mediaInfo = info->info;
        else if (const auto* state = std::get_if<media_sdk::StateChangedEvent>(&event.payload)) {
            if (state->state == media_sdk::PlayerState::Stopped)
                m_stopped = true;
        }
        else if (const auto* error = std::get_if<media_sdk::ErrorEvent>(&event.payload)) {
            m_error = error->error.message;
            m_finished = true;
        } else if (std::holds_alternative<media_sdk::EndOfFileEvent>(event.payload)) {
            m_finished = true;
        }
#if defined(MEDIA_SDK_BENCHMARK_HAS_POOL_DIAGNOSTICS)
        else if (const auto* report = std::get_if<media_sdk::DecodePerformanceEvent>(&event.payload)) {
            ++m_reportCount;
            m_reportedVideoFrames += static_cast<std::uint64_t>(
                std::max<std::int64_t>(report->decodedVideoFrames, 0));
            m_normalizeWeightedUs += static_cast<long double>(report->normalizeAverageUs)
                * static_cast<long double>(std::max<std::int64_t>(report->decodedVideoFrames, 0));
            m_normalizeMaxUs = std::max(m_normalizeMaxUs, report->normalizeMaxUs);
        }
#endif
        m_changed.notify_all();
    }

    media_sdk::DecodeFramePushResult pushAudio(
        media_sdk::AudioFrame,
        media_sdk::DecodeFrameMetadata) override
    {
        std::lock_guard lock(m_mutex);
        ++m_audioFrames;
        return { .status = media_sdk::DecodeFramePushStatus::Accepted };
    }

    media_sdk::DecodeFramePushResult pushVideo(
        media_sdk::VideoFrame frame,
        media_sdk::DecodeFrameMetadata) override
    {
        std::lock_guard lock(m_mutex);
        if (m_maxVideoFrames > 0 && m_videoFrames >= m_maxVideoFrames)
            return { .status = media_sdk::DecodeFramePushStatus::Cancelled };
        ++m_videoFrames;
        ++m_pixelFormats[pixelFormatName(frame.pixelFormat())];
        for (const auto& plane : frame.planes()) {
            if (!plane.data || plane.height <= 0 || plane.stride == 0)
                continue;
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(plane.data);
            m_checksum ^= bytes[0];
            m_checksum *= 1'099'511'628'211ULL;
        }
        if (m_holdVideoFrames > 0) {
            m_heldVideoFrames.push_back(std::move(frame));
            while (m_heldVideoFrames.size() > m_holdVideoFrames)
                m_heldVideoFrames.pop_front();
        }
        if (m_maxVideoFrames > 0 && m_videoFrames >= m_maxVideoFrames) {
            m_finished = true;
            m_changed.notify_all();
        }
        return { .status = media_sdk::DecodeFramePushStatus::Accepted };
    }

    bool waitUntilFinished(std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, timeout, [this]() { return m_finished; });
    }

    bool waitUntilStopped(std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, timeout, [this]() { return m_stopped; });
    }

    void releaseHeldFrames()
    {
        std::lock_guard lock(m_mutex);
        m_heldVideoFrames.clear();
    }

    struct Snapshot {
        media_sdk::MediaInfo mediaInfo;
        std::optional<std::string> error;
        std::uint64_t audioFrames = 0;
        std::uint64_t videoFrames = 0;
        std::uint64_t checksum = 0;
        std::map<std::string, std::uint64_t> pixelFormats;
        std::uint64_t reportCount = 0;
        std::uint64_t reportedVideoFrames = 0;
        long double normalizeWeightedUs = 0;
        std::int64_t normalizeMaxUs = 0;
    };

    Snapshot snapshot() const
    {
        std::lock_guard lock(m_mutex);
        return {
            .mediaInfo = m_mediaInfo,
            .error = m_error,
            .audioFrames = m_audioFrames,
            .videoFrames = m_videoFrames,
            .checksum = m_checksum,
            .pixelFormats = m_pixelFormats,
            .reportCount = m_reportCount,
            .reportedVideoFrames = m_reportedVideoFrames,
            .normalizeWeightedUs = m_normalizeWeightedUs,
            .normalizeMaxUs = m_normalizeMaxUs,
        };
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::size_t m_holdVideoFrames = 0;
    std::uint64_t m_maxVideoFrames = 0;
    std::deque<media_sdk::VideoFrame> m_heldVideoFrames;
    media_sdk::MediaInfo m_mediaInfo;
    std::optional<std::string> m_error;
    std::map<std::string, std::uint64_t> m_pixelFormats;
    std::uint64_t m_audioFrames = 0;
    std::uint64_t m_videoFrames = 0;
    std::uint64_t m_checksum = 1'469'598'103'934'665'603ULL;
    std::uint64_t m_reportCount = 0;
    std::uint64_t m_reportedVideoFrames = 0;
    long double m_normalizeWeightedUs = 0;
    std::int64_t m_normalizeMaxUs = 0;
    bool m_finished = false;
    bool m_stopped = false;
};

void writePoolSnapshot(std::ostream& output,
                       const PoolSnapshot& pool)
{
    output << "{\"acquire_count\":" << pool.acquireCount
           << ",\"reuse_count\":" << pool.reuseCount
           << ",\"allocation_count\":" << pool.allocationCount
           << ",\"transient_allocation_count\":" << pool.transientAllocationCount
           << ",\"high_watermark\":" << pool.highWatermark
           << ",\"retained_count\":" << pool.retainedCount
           << ",\"in_flight_count\":" << pool.inFlightCount << '}';
}

} // namespace

int main(int argc, char** argv)
{
    const auto options = parseOptions(argc, argv);
    if (!options.has_value()) {
        std::cerr << "Usage: MediaSdkVideoBenchmark --input FILE --output FILE --label NAME "
                     "[--software|--hardware] [--hold-video-frames N] [--timeout-ms N] "
                     "[--max-video-frames N] [--report-interval-ms N]\n";
        return 2;
    }
    if (!std::filesystem::is_regular_file(options->input)) {
        std::cerr << "Input is not a regular file: " << options->input << '\n';
        return 2;
    }

    rusage usageBefore {};
    getrusage(RUSAGE_SELF, &usageBefore);
    const auto started = std::chrono::steady_clock::now();

    BenchmarkSink sink(options->holdVideoFrames, options->maxVideoFrames);
    media_sdk::PlayerConfig config;
    config.enableHardwareDecode = options->hardwareDecode;
    config.preferNativeVideoFrames = options->hardwareDecode;
#if defined(MEDIA_SDK_BENCHMARK_HAS_POOL_DIAGNOSTICS)
    config.decodePerformanceReportInterval = options->reportInterval;
#endif
    media_sdk::Player player(config, sink, sink);

    const auto openResult = player.open(options->input);
    if (!openResult.ok()) {
        std::cerr << "Open submission failed: " << openResult.error().message << '\n';
        return 1;
    }
    player.play();
    const bool finished = sink.waitUntilFinished(options->timeout);

    PoolSnapshot poolBeforeRelease;
    PoolSnapshot poolAfterRelease;
#if defined(MEDIA_SDK_BENCHMARK_HAS_POOL_DIAGNOSTICS)
    const auto beforeRelease = player.diagnostics().videoPicturePool;
    poolBeforeRelease = {
        .acquireCount = beforeRelease.acquireCount,
        .reuseCount = beforeRelease.reuseCount,
        .allocationCount = beforeRelease.allocationCount,
        .transientAllocationCount = beforeRelease.transientAllocationCount,
        .highWatermark = beforeRelease.highWatermark,
        .retainedCount = beforeRelease.retainedCount,
        .inFlightCount = beforeRelease.inFlightCount,
    };
#endif
    player.stop();
    const bool stopped = sink.waitUntilStopped(5s);
    sink.releaseHeldFrames();
#if defined(MEDIA_SDK_BENCHMARK_HAS_POOL_DIAGNOSTICS)
    const auto afterRelease = player.diagnostics().videoPicturePool;
    poolAfterRelease = {
        .acquireCount = poolBeforeRelease.acquireCount,
        .reuseCount = poolBeforeRelease.reuseCount,
        .allocationCount = poolBeforeRelease.allocationCount,
        .transientAllocationCount = poolBeforeRelease.transientAllocationCount,
        .highWatermark = poolBeforeRelease.highWatermark,
        .retainedCount = afterRelease.retainedCount,
        .inFlightCount = afterRelease.inFlightCount,
    };
#endif
    const auto elapsed = std::chrono::steady_clock::now() - started;
    rusage usageAfter {};
    getrusage(RUSAGE_SELF, &usageAfter);
    const auto snapshot = sink.snapshot();

    std::filesystem::create_directories(options->output.parent_path());
    std::ofstream output(options->output);
    if (!output) {
        std::cerr << "Cannot open output: " << options->output << '\n';
        return 1;
    }

    const auto wallMs = std::chrono::duration<double, std::milli>(elapsed).count();
    const auto userSeconds = timevalSeconds(usageAfter.ru_utime) - timevalSeconds(usageBefore.ru_utime);
    const auto systemSeconds = timevalSeconds(usageAfter.ru_stime) - timevalSeconds(usageBefore.ru_stime);
    const auto normalizeAverageUs = snapshot.reportedVideoFrames > 0
        ? static_cast<double>(snapshot.normalizeWeightedUs
                              / static_cast<long double>(snapshot.reportedVideoFrames))
        : 0.0;

    output << std::fixed << std::setprecision(3)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"label\": \"" << jsonEscape(options->label) << "\",\n"
           << "  \"input\": \"" << jsonEscape(std::filesystem::absolute(options->input).string()) << "\",\n"
           << "  \"system\": \"" << jsonEscape(systemName()) << "\",\n"
           << "  \"hardware_decode\": " << (options->hardwareDecode ? "true" : "false") << ",\n"
           << "  \"hold_video_frames\": " << options->holdVideoFrames << ",\n"
           << "  \"max_video_frames\": " << options->maxVideoFrames << ",\n"
           << "  \"completed\": "
           << (finished && stopped && !snapshot.error.has_value() ? "true" : "false") << ",\n"
           << "  \"error\": ";
    if (snapshot.error)
        output << '"' << jsonEscape(*snapshot.error) << '"';
    else if (!finished)
        output << "\"timeout\"";
    else
        output << "null";
    output << ",\n"
           << "  \"media\": {\"duration_ms\":" << snapshot.mediaInfo.duration.count()
           << ",\"width\":" << snapshot.mediaInfo.width
           << ",\"height\":" << snapshot.mediaInfo.height
           << ",\"fps\":" << snapshot.mediaInfo.fps
           << ",\"format\":\"" << jsonEscape(snapshot.mediaInfo.formatName) << "\"},\n"
           << "  \"frames\": {\"video\":" << snapshot.videoFrames
           << ",\"audio\":" << snapshot.audioFrames
           << ",\"checksum\":" << snapshot.checksum << "},\n"
           << "  \"pixel_formats\": {";
    bool firstFormat = true;
    for (const auto& [name, count] : snapshot.pixelFormats) {
        if (!firstFormat) output << ',';
        output << '\"' << jsonEscape(name) << "\":" << count;
        firstFormat = false;
    }
    output << "},\n"
           << "  \"timing\": {\"wall_ms\":" << wallMs
           << ",\"user_cpu_ms\":" << userSeconds * 1000.0
           << ",\"system_cpu_ms\":" << systemSeconds * 1000.0
           << ",\"max_rss_bytes\":" << maxRssBytes(usageAfter) << "},\n"
           << "  \"decode_report\": {\"count\":" << snapshot.reportCount
           << ",\"reported_video_frames\":" << snapshot.reportedVideoFrames
           << ",\"normalize_average_us\":" << normalizeAverageUs
           << ",\"normalize_max_us\":" << snapshot.normalizeMaxUs << "},\n"
           << "  \"pool_before_release\": ";
    writePoolSnapshot(output, poolBeforeRelease);
    output << ",\n  \"pool_after_release\": ";
    writePoolSnapshot(output, poolAfterRelease);
    output << "\n}\n";

    if (!finished || !stopped) {
        std::cerr << "Benchmark timed out\n";
        return 1;
    }
    if (snapshot.error) {
        std::cerr << "Decode failed: " << *snapshot.error << '\n';
        return 1;
    }
    return 0;
}
