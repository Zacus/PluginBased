#include "media_sdk/session/PlaybackSession.h"
#include "media_sdk/audio/ffmpeg/FfmpegAudioTempoProcessor.h"
#include "media_sdk/runtime/PlaybackRate.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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
#if defined(__APPLE__)
#include <mach/mach.h>
#endif
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

struct Options {
    std::filesystem::path input;
    std::filesystem::path output;
    std::string label = "unlabelled";
    std::uint64_t maxPresentedFrames = 600;
    std::chrono::milliseconds maxMediaDuration { 0 };
    std::chrono::milliseconds timeout { 30'000 };
    double playbackRate = media_sdk::runtime::kDefaultPlaybackRate;
    double expectedToneHz = 0.0;
    std::string scenario = "steady";
    bool hardwareDecode = false;
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
            } else if (argument == "--max-media-ms") {
                const auto value = nextValue(i, argc, argv);
                if (!value) return std::nullopt;
                options.maxMediaDuration = std::chrono::milliseconds(std::stoll(*value));
            } else if (argument == "--playback-rate") {
                const auto value = nextValue(i, argc, argv);
                if (!value) return std::nullopt;
                options.playbackRate = std::stod(*value);
            } else if (argument == "--expected-tone-hz") {
                const auto value = nextValue(i, argc, argv);
                if (!value) return std::nullopt;
                options.expectedToneHz = std::stod(*value);
            } else if (argument == "--scenario") {
                const auto value = nextValue(i, argc, argv);
                if (!value) return std::nullopt;
                options.scenario = *value;
            } else if (argument == "--hardware") {
                options.hardwareDecode = true;
            } else {
                return std::nullopt;
            }
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    const bool validScenario = options.scenario == "steady"
        || options.scenario == "playing-change"
        || options.scenario == "paused-change"
        || options.scenario == "seek-change"
        || options.scenario == "continuous-change";
    if (options.input.empty() || options.output.empty()
        || (options.maxPresentedFrames == 0 && options.maxMediaDuration <= 0ms)
        || !media_sdk::runtime::isPlaybackRateSupported(options.playbackRate)
        || options.expectedToneHz < 0.0 || !validScenario) {
        return std::nullopt;
    }
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

std::uint64_t currentRssBytes()
{
#if defined(__APPLE__)
    mach_task_basic_info_data_t info {};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
        return 0;
    }
    return static_cast<std::uint64_t>(info.resident_size);
#else
    return 0;
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
    void setBenchmarkStart(std::chrono::steady_clock::time_point startedAt)
    {
        std::lock_guard lock(m_mutex);
        m_benchmarkStartedAt = startedAt;
    }

    media_sdk::Result<void> open(const media_sdk::runtime::AudioFormat& format) override
    {
        std::lock_guard lock(m_mutex);
        m_format = format;
        m_open = true;
        m_paused = true;
        m_everResumed = false;
        m_anchor.reset();
        return media_sdk::Result<void>::success();
    }

    media_sdk::Result<void> write(media_sdk::runtime::AudioBufferView buffer) override
    {
        std::unique_lock lock(m_mutex);
        if (!m_everResumed)
            m_changed.wait(lock, [this]() { return !m_open || !m_paused; });
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
                .playbackRate = buffer.playbackRate,
            };
        }
        const auto mediaOffset = buffer.pts - m_anchor->media;
        const auto target = m_anchor->wall + std::chrono::microseconds {
            static_cast<std::int64_t>(static_cast<double>(mediaOffset.count())
                                      / m_anchor->playbackRate) };
        const auto arrivedAt = std::chrono::steady_clock::now();
        if (!m_firstWriteAt.has_value())
            m_firstWriteAt = arrivedAt;
        if (arrivedAt > target + 20ms)
            ++m_lateWriteCount;

        if (m_format.sampleFormat == media_sdk::runtime::AudioSampleFormat::Float32
            && m_format.channels > 0
            && buffer.bytes.size() % (sizeof(float) * static_cast<std::size_t>(m_format.channels)) == 0) {
            const auto* samples = reinterpret_cast<const float*>(buffer.bytes.data());
            const auto frameCount = buffer.bytes.size()
                / (sizeof(float) * static_cast<std::size_t>(m_format.channels));
            const auto remaining = m_maxCapturedSamples - std::min(m_capturedSamples.size(), m_maxCapturedSamples);
            const auto capturedFrames = std::min(frameCount, remaining);
            for (std::size_t frame = 0; frame < capturedFrames; ++frame)
                m_capturedSamples.push_back(samples[frame * static_cast<std::size_t>(m_format.channels)]);
        }
        while (m_open) {
            if (m_paused)
                break;
            if (!m_changed.wait_until(lock, target, [this]() { return !m_open || m_paused; }))
                break;
        }
        if (!m_open) {
            return media_sdk::Result<void>::failure({
                media_sdk::MediaErrorCode::InternalStateError,
                "Realtime benchmark audio output closed while writing",
                {},
            });
        }
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
        const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(elapsed);
        return {
            .position = m_anchor->media
                + std::chrono::microseconds { static_cast<std::int64_t>(
                    static_cast<double>(elapsedUs.count()) * m_anchor->playbackRate) },
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
        m_everResumed = true;
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

    bool waitForPosition(std::chrono::milliseconds position,
                         std::chrono::steady_clock::time_point deadline) const
    {
        while (std::chrono::steady_clock::now() < deadline) {
            const auto snapshot = clock();
            if (snapshot.valid
                && snapshot.position >= std::chrono::duration_cast<std::chrono::microseconds>(position)) {
                return true;
            }
            std::this_thread::sleep_for(5ms);
        }
        return false;
    }

    struct Snapshot {
        std::uint64_t underflowCount = 0;
        std::uint64_t lateWriteCount = 0;
        double startupMs = 0.0;
        double estimatedToneHz = 0.0;
        std::size_t capturedSamples = 0;
        std::int64_t positionMs = 0;
    };

    Snapshot snapshot() const
    {
        std::lock_guard lock(m_mutex);
        double estimatedToneHz = 0.0;
        if (m_format.sampleRate > 0 && m_capturedSamples.size() > 2) {
            const auto skipped = std::min<std::size_t>(
                m_capturedSamples.size(), static_cast<std::size_t>(m_format.sampleRate / 4));
            std::uint64_t positiveCrossings = 0;
            for (std::size_t index = std::max<std::size_t>(skipped, 1);
                 index < m_capturedSamples.size(); ++index) {
                if (m_capturedSamples[index - 1] <= 0.0f && m_capturedSamples[index] > 0.0f)
                    ++positiveCrossings;
            }
            const auto measuredSamples = m_capturedSamples.size() - skipped;
            if (measuredSamples > 0) {
                estimatedToneHz = static_cast<double>(positiveCrossings)
                    * static_cast<double>(m_format.sampleRate)
                    / static_cast<double>(measuredSamples);
            }
        }
        const double startupMs = m_firstWriteAt.has_value()
            ? std::chrono::duration<double, std::milli>(*m_firstWriteAt - m_benchmarkStartedAt).count()
            : 0.0;
        std::int64_t positionMs = 0;
        if (m_open && m_anchor.has_value()) {
            const auto elapsed = m_paused
                ? m_pausedAt - m_anchor->wall
                : std::chrono::steady_clock::now() - m_anchor->wall;
            const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(elapsed);
            const auto position = m_anchor->media + std::chrono::microseconds {
                static_cast<std::int64_t>(static_cast<double>(elapsedUs.count())
                                          * m_anchor->playbackRate) };
            positionMs = std::chrono::duration_cast<std::chrono::milliseconds>(position).count();
        }
        return {
            .underflowCount = 0,
            .lateWriteCount = m_lateWriteCount,
            .startupMs = startupMs,
            .estimatedToneHz = estimatedToneHz,
            .capturedSamples = m_capturedSamples.size(),
            .positionMs = positionMs,
        };
    }

private:
    struct Anchor {
        std::chrono::microseconds media { 0 };
        std::chrono::steady_clock::time_point wall;
        media_sdk::runtime::Generation generation = 0;
        double playbackRate = media_sdk::runtime::kDefaultPlaybackRate;
    };

    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    media_sdk::runtime::AudioFormat m_format;
    std::optional<Anchor> m_anchor;
    std::optional<std::chrono::steady_clock::time_point> m_firstWriteAt;
    std::chrono::steady_clock::time_point m_benchmarkStartedAt = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point m_pausedAt = std::chrono::steady_clock::now();
    std::vector<float> m_capturedSamples;
    std::uint64_t m_lateWriteCount = 0;
    static constexpr std::size_t m_maxCapturedSamples = 2'000'000;
    bool m_open = false;
    bool m_paused = true;
    bool m_everResumed = false;
};

class BenchmarkPresenter final : public media_sdk::runtime::IVideoPresenter {
public:
    BenchmarkPresenter(std::uint64_t maxFrames, const RealtimeAudioOutput* audioOutput)
        : m_maxFrames(maxFrames)
        , m_audioOutput(audioOutput)
    {
    }

    void setBenchmarkStart(std::chrono::steady_clock::time_point startedAt)
    {
        std::lock_guard lock(m_mutex);
        m_benchmarkStartedAt = startedAt;
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
        if (m_maxFrames > 0 && m_presentedFrames >= m_maxFrames)
            return { .status = media_sdk::runtime::PresentStatus::Skipped };

        if (!m_firstPresentAt.has_value())
            m_firstPresentAt = std::chrono::steady_clock::now();
        ++m_presentedFrames;
        m_lastPresentedPts = frame.pts();
        m_latenessTotalUs += timing.lateness.count();
        m_latenessMaxUs = std::max(m_latenessMaxUs, timing.lateness.count());
        if (m_audioOutput && m_presentedFrames > 15) {
            const auto audioClock = m_audioOutput->clock();
            if (audioClock.valid) {
                const auto drift = std::llabs((frame.pts() - audioClock.position).count());
                m_avDriftAbsoluteTotalUs += drift;
                m_avDriftAbsoluteMaxUs = std::max(m_avDriftAbsoluteMaxUs, drift);
                ++m_avDriftSamples;
            }
        }
        for (const auto& plane : frame.planes()) {
            if (!plane.data)
                continue;
            m_checksum ^= static_cast<std::uint64_t>(
                *reinterpret_cast<const std::uint8_t*>(plane.data));
            m_checksum *= 1'099'511'628'211ULL;
        }
        if (m_maxFrames > 0 && m_presentedFrames == m_maxFrames)
            m_changed.notify_all();
        else if (m_maxFrames == 0)
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

    bool waitForPosition(std::chrono::milliseconds position,
                         std::chrono::steady_clock::time_point deadline)
    {
        const auto target = std::chrono::duration_cast<std::chrono::microseconds>(position);
        std::unique_lock lock(m_mutex);
        return m_changed.wait_until(lock, deadline, [this, target]() {
            return m_lastPresentedPts >= target;
        });
    }

    struct Snapshot {
        std::uint64_t presentedFrames = 0;
        std::uint64_t checksum = 0;
        std::int64_t latenessTotalUs = 0;
        std::int64_t latenessMaxUs = 0;
        std::uint64_t avDriftSamples = 0;
        std::int64_t avDriftAbsoluteTotalUs = 0;
        std::int64_t avDriftAbsoluteMaxUs = 0;
        double startupMs = 0.0;
        std::int64_t lastPresentedPtsMs = 0;
    };

    Snapshot snapshot() const
    {
        std::lock_guard lock(m_mutex);
        const double startupMs = m_firstPresentAt.has_value()
            ? std::chrono::duration<double, std::milli>(*m_firstPresentAt - m_benchmarkStartedAt).count()
            : 0.0;
        return {
            .presentedFrames = m_presentedFrames,
            .checksum = m_checksum,
            .latenessTotalUs = m_latenessTotalUs,
            .latenessMaxUs = m_latenessMaxUs,
            .avDriftSamples = m_avDriftSamples,
            .avDriftAbsoluteTotalUs = m_avDriftAbsoluteTotalUs,
            .avDriftAbsoluteMaxUs = m_avDriftAbsoluteMaxUs,
            .startupMs = startupMs,
            .lastPresentedPtsMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                m_lastPresentedPts).count(),
        };
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    media_sdk::runtime::IVideoPresenterEvents* m_events = nullptr;
    std::uint64_t m_maxFrames = 0;
    const RealtimeAudioOutput* m_audioOutput = nullptr;
    std::uint64_t m_presentedFrames = 0;
    std::uint64_t m_checksum = 1'469'598'103'934'665'603ULL;
    std::int64_t m_latenessTotalUs = 0;
    std::int64_t m_latenessMaxUs = 0;
    std::uint64_t m_avDriftSamples = 0;
    std::int64_t m_avDriftAbsoluteTotalUs = 0;
    std::int64_t m_avDriftAbsoluteMaxUs = 0;
    std::optional<std::chrono::steady_clock::time_point> m_firstPresentAt;
    std::chrono::microseconds m_lastPresentedPts { 0 };
    std::chrono::steady_clock::time_point m_benchmarkStartedAt = std::chrono::steady_clock::now();
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
        } else if (const auto* position = std::get_if<media_sdk::PositionChangedEvent>(&event.payload)) {
            m_position = position->position;
        } else if (const auto* state = std::get_if<media_sdk::StateChangedEvent>(&event.payload)) {
            m_finished = state->state == media_sdk::PlayerState::Finished;
        } else if (std::holds_alternative<media_sdk::PlaybackRateChangedEvent>(event.payload)) {
            ++m_rateChangeCount;
        } else if (std::holds_alternative<media_sdk::SeekCompletedEvent>(event.payload)) {
            ++m_seekCompletedCount;
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

    bool waitForPosition(std::chrono::milliseconds position,
                         std::chrono::steady_clock::time_point deadline)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_until(lock, deadline, [this, position]() {
            return m_position >= position || m_finished || m_error.has_value();
        }) && m_position >= position;
    }

    bool waitForFinished(std::chrono::steady_clock::time_point deadline)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_until(lock, deadline, [this]() {
            return m_finished || m_error.has_value();
        }) && m_finished;
    }

    bool waitForSeek(int previousCount, std::chrono::steady_clock::time_point deadline)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_until(lock, deadline, [this, previousCount]() {
            return m_seekCompletedCount > previousCount || m_error.has_value();
        }) && m_seekCompletedCount > previousCount;
    }

    struct Snapshot {
        std::chrono::milliseconds position { 0 };
        int rateChangeCount = 0;
        int seekCompletedCount = 0;
        bool finished = false;
    };

    Snapshot snapshot() const
    {
        std::lock_guard lock(m_mutex);
        return {
            .position = m_position,
            .rateChangeCount = m_rateChangeCount,
            .seekCompletedCount = m_seekCompletedCount,
            .finished = m_finished,
        };
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    media_sdk::MediaInfo m_mediaInfo;
    media_sdk::runtime::RuntimeDiagnostics m_lastDiagnostics;
    std::optional<std::string> m_error;
    std::chrono::milliseconds m_position { 0 };
    int m_rateChangeCount = 0;
    int m_seekCompletedCount = 0;
    bool m_ready = false;
    bool m_finished = false;
};

} // namespace

int main(int argc, char** argv)
{
    const auto options = parseOptions(argc, argv);
    if (!options) {
        std::cerr << "Usage: MediaSdkRealtimeVideoBenchmark --input FILE --output FILE "
                     "--label NAME [--max-presented-frames N | --max-media-ms N] "
                     "[--timeout-ms N] [--playback-rate RATE] [--expected-tone-hz HZ] "
                     "[--scenario steady|playing-change|paused-change|seek-change|continuous-change] "
                     "[--hardware]\n";
        return 2;
    }

    RealtimeAudioOutput audioOutput;
    BenchmarkPresenter presenter(options->maxPresentedFrames, &audioOutput);
    BenchmarkEvents events;
    media_sdk::audio::ffmpeg::FfmpegAudioTempoProcessor tempoProcessor;
    media_sdk::session::PlaybackSessionConfig config;
    config.core.enableHardwareDecode = options->hardwareDecode;
    config.core.preferNativeVideoFrames = false;
    config.preferNativeVideoFrames = false;
    config.runtime.outputPolicy = media_sdk::runtime::VideoOutputPolicy::CpuOnly;
    config.runtime.videoQueueCapacity = 8;
    config.runtime.audioQueueCapacity = 32;
    config.runtime.playbackRate = options->scenario == "steady"
        ? options->playbackRate
        : media_sdk::runtime::kDefaultPlaybackRate;

    media_sdk::session::PlaybackSession session(config, {
        .audioOutput = &audioOutput,
        .videoPresenter = &presenter,
        .events = &events,
        .audioTempoProcessor = &tempoProcessor,
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
    const auto mediaInfo = events.mediaInfo();

    rusage usageBefore {};
    getrusage(RUSAGE_SELF, &usageBefore);
    const auto rssBefore = currentRssBytes();
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + options->timeout;
    audioOutput.setBenchmarkStart(started);
    presenter.setBenchmarkStart(started);
    session.play();

    bool controlsCompleted = true;
    const auto runRateChange = [&](double playbackRate) {
        const auto result = session.setPlaybackRate(playbackRate);
        if (!result.ok()) {
            std::cerr << "Playback rate change failed: " << result.error().message << '\n';
            controlsCompleted = false;
        }
    };
    if (options->scenario != "steady") {
        const auto firstCut = std::min(1'000ms, options->maxMediaDuration / 4);
        controlsCompleted = events.waitForPosition(firstCut, deadline);
        if (controlsCompleted && options->scenario == "playing-change") {
            runRateChange(options->playbackRate);
        } else if (controlsCompleted && options->scenario == "paused-change") {
            session.pause();
            runRateChange(options->playbackRate);
            std::this_thread::sleep_for(100ms);
            session.play();
        } else if (controlsCompleted && options->scenario == "seek-change") {
            const auto seekTarget = std::min(options->maxMediaDuration / 2,
                                             mediaInfo.duration - 100ms);
            const int previousSeekCount = events.snapshot().seekCompletedCount;
            const auto seekResult = session.seek(seekTarget);
            controlsCompleted = seekResult.ok()
                && events.waitForSeek(previousSeekCount, deadline);
            if (controlsCompleted)
                runRateChange(options->playbackRate);
        } else if (controlsCompleted && options->scenario == "continuous-change") {
            runRateChange(1.25);
            controlsCompleted = events.waitForPosition(firstCut + 750ms, deadline);
            if (controlsCompleted)
                runRateChange(0.75);
            controlsCompleted = controlsCompleted
                && events.waitForPosition(firstCut + 1'500ms, deadline);
            if (controlsCompleted)
                runRateChange(options->playbackRate);
        }
    }

    bool reachedTarget = false;
    std::chrono::milliseconds mediaTarget { 0 };
    auto rssMeasurementStart = rssBefore;
    if (controlsCompleted && options->maxMediaDuration > 0ms) {
        mediaTarget = std::min(options->maxMediaDuration,
                               std::max(0ms, mediaInfo.duration - 50ms));
        const auto waitForMediaPosition = [&](std::chrono::milliseconds target) {
            if (mediaInfo.sampleRate > 0 && mediaInfo.channels > 0)
                return audioOutput.waitForPosition(target, deadline);
            if (mediaInfo.width > 0 && mediaInfo.height > 0)
                return presenter.waitForPosition(target, deadline);
            return events.waitForPosition(target, deadline);
        };
        if (options->scenario == "steady") {
            reachedTarget = waitForMediaPosition(mediaTarget / 2);
            if (reachedTarget)
                rssMeasurementStart = currentRssBytes();
        } else {
            rssMeasurementStart = currentRssBytes();
            reachedTarget = true;
        }
        if (reachedTarget)
            reachedTarget = waitForMediaPosition(mediaTarget);
    } else if (controlsCompleted) {
        reachedTarget = presenter.waitForTarget(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline
                                                                  - std::chrono::steady_clock::now()));
    }
    const bool completed = reachedTarget && controlsCompleted && !events.error().has_value();
    const auto diagnostics = session.diagnostics();
    const auto eventSnapshot = events.snapshot();
    const auto audioSnapshot = audioOutput.snapshot();
    const auto presenterSnapshot = presenter.snapshot();
    session.stop();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    rusage usageAfter {};
    getrusage(RUSAGE_SELF, &usageAfter);
    const auto rssAfter = currentRssBytes();

    const auto wallMs = std::chrono::duration<double, std::milli>(elapsed).count();
    const auto userMs = (timevalSeconds(usageAfter.ru_utime) - timevalSeconds(usageBefore.ru_utime)) * 1000.0;
    const auto systemMs = (timevalSeconds(usageAfter.ru_stime) - timevalSeconds(usageBefore.ru_stime)) * 1000.0;
    const auto latenessAverageUs = presenterSnapshot.presentedFrames > 0
        ? static_cast<double>(presenterSnapshot.latenessTotalUs)
            / static_cast<double>(presenterSnapshot.presentedFrames)
        : 0.0;
    const auto avDriftAverageUs = presenterSnapshot.avDriftSamples > 0
        ? static_cast<double>(presenterSnapshot.avDriftAbsoluteTotalUs)
            / static_cast<double>(presenterSnapshot.avDriftSamples)
        : 0.0;
    const double expectedWallMs = options->scenario == "steady"
        && options->maxMediaDuration > 0ms
        ? static_cast<double>(std::min(options->maxMediaDuration, mediaInfo.duration).count())
            / options->playbackRate
        : 0.0;
    const auto measuredPositionMs = audioSnapshot.positionMs > 0
        ? audioSnapshot.positionMs
        : (presenterSnapshot.lastPresentedPtsMs > 0
            ? presenterSnapshot.lastPresentedPtsMs
            : eventSnapshot.position.count());

    std::filesystem::create_directories(options->output.parent_path());
    std::ofstream output(options->output);
    if (!output) {
        std::cerr << "Cannot open output: " << options->output << '\n';
        return 1;
    }
    output << std::fixed << std::setprecision(3)
           << "{\n"
           << "  \"schema_version\": 2,\n"
           << "  \"benchmark_kind\": \"realtime_pipeline\",\n"
           << "  \"label\": \"" << jsonEscape(options->label) << "\",\n"
           << "  \"input\": \"" << jsonEscape(std::filesystem::absolute(options->input).string()) << "\",\n"
           << "  \"system\": \"" << jsonEscape(systemName()) << "\",\n"
           << "  \"completed\": " << (completed ? "true" : "false") << ",\n"
           << "  \"scenario\": {\"name\":\"" << jsonEscape(options->scenario)
           << "\",\"requested_rate\":" << options->playbackRate
           << ",\"confirmed_rate_changes\":" << eventSnapshot.rateChangeCount
           << ",\"seek_completions\":" << eventSnapshot.seekCompletedCount << "},\n"
           << "  \"media\": {\"duration_ms\":" << mediaInfo.duration.count()
           << ",\"width\":" << mediaInfo.width
           << ",\"height\":" << mediaInfo.height
           << ",\"fps\":" << mediaInfo.fps
           << ",\"format\":\"" << jsonEscape(mediaInfo.formatName) << "\"},\n"
           << "  \"presenter\": {\"presented_frames\":" << presenterSnapshot.presentedFrames
           << ",\"checksum\":" << presenterSnapshot.checksum
           << ",\"lateness_average_us\":" << latenessAverageUs
           << ",\"lateness_max_us\":" << presenterSnapshot.latenessMaxUs
           << ",\"av_drift_samples\":" << presenterSnapshot.avDriftSamples
           << ",\"av_drift_abs_average_us\":" << avDriftAverageUs
           << ",\"av_drift_abs_max_us\":" << presenterSnapshot.avDriftAbsoluteMaxUs
           << ",\"startup_ms\":" << presenterSnapshot.startupMs << "},\n"
           << "  \"audio\": {\"captured_samples\":" << audioSnapshot.capturedSamples
           << ",\"estimated_tone_hz\":" << audioSnapshot.estimatedToneHz
           << ",\"expected_tone_hz\":" << options->expectedToneHz
           << ",\"underflow_count\":" << audioSnapshot.underflowCount
           << ",\"late_write_count\":" << audioSnapshot.lateWriteCount
           << ",\"startup_ms\":" << audioSnapshot.startupMs << "},\n"
           << "  \"timing\": {\"wall_ms\":" << wallMs
           << ",\"expected_wall_ms\":" << expectedWallMs
           << ",\"media_position_ms\":" << measuredPositionMs
           << ",\"user_cpu_ms\":" << userMs
           << ",\"system_cpu_ms\":" << systemMs
           << ",\"max_rss_bytes\":" << maxRssBytes(usageAfter)
           << ",\"rss_play_start_bytes\":" << rssBefore
           << ",\"rss_before_bytes\":" << rssMeasurementStart
           << ",\"rss_after_bytes\":" << rssAfter
           << ",\"rss_growth_bytes\":"
           << (static_cast<std::int64_t>(rssAfter)
               - static_cast<std::int64_t>(rssMeasurementStart))
           << "},\n"
           << "  \"runtime\": {\"video_queued\":" << diagnostics.videoQueued
           << ",\"playback_rate\":" << diagnostics.playbackRate
           << ",\"playback_rate_change_count\":" << diagnostics.playbackRateChangeCount
           << ",\"video_presented\":" << diagnostics.videoPresented
           << ",\"video_dropped_late\":" << diagnostics.videoDroppedLate
           << ",\"video_backpressure_count\":" << diagnostics.videoBackpressureCount
           << ",\"video_queue_high_watermark\":" << diagnostics.videoQueueHighWatermark
           << ",\"audio_backpressure_count\":" << diagnostics.audioBackpressureCount
           << ",\"audio_queue_high_watermark\":" << diagnostics.audioQueueHighWatermark
           << ",\"audio_tempo_input_samples\":" << diagnostics.audioTempoInputSamples
           << ",\"audio_tempo_output_samples\":" << diagnostics.audioTempoOutputSamples
           << ",\"audio_tempo_failure_count\":" << diagnostics.audioTempoFailureCount
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
