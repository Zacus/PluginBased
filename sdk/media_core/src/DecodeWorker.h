#pragma once

#include "Demuxer.h"
#include "SeekPrerollGate.h"
#include "StreamDecoder.h"
#include "VideoFrameProcessor.h"
#include "media_sdk/DecodeFrameSink.h"
#include "media_sdk/Player.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
#include <stop_token>
#endif

namespace media_sdk {

// Prefer std::jthread/std::stop_token when the toolchain provides them. The
// current macOS libc++ in this workspace does not, so keep an equivalent local
// fallback with the same request_stop/joinable surface.
#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
using WorkerStopToken = std::stop_token;
using WorkerThread = std::jthread;
#else
class WorkerStopToken
{
public:
    explicit WorkerStopToken(const std::atomic_bool& stopRequested)
        : m_stopRequested(stopRequested)
    {
    }

    bool stop_requested() const
    {
        return m_stopRequested.load();
    }

private:
    const std::atomic_bool& m_stopRequested;
};

class WorkerThread
{
public:
    WorkerThread() = default;

    template<typename Function>
    explicit WorkerThread(Function&& function)
    {
        m_thread = std::thread([this, fn = std::forward<Function>(function)]() mutable {
            fn(WorkerStopToken(m_stopRequested));
        });
    }

    ~WorkerThread()
    {
        request_stop();
        if (m_thread.joinable())
            m_thread.join();
    }

    WorkerThread(const WorkerThread&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;

    WorkerThread(WorkerThread&&) noexcept = delete;
    WorkerThread& operator=(WorkerThread&&) noexcept = delete;

    bool joinable() const
    {
        return m_thread.joinable();
    }

    void request_stop()
    {
        m_stopRequested.store(true);
    }

private:
    std::atomic_bool m_stopRequested { false };
    std::thread m_thread;
};
#endif

class DecodeWorker
{
public:
    DecodeWorker(PlayerConfig config, IEventSink& events, IDecodeFrameSink& frames);
    ~DecodeWorker();

    DecodeWorker(const DecodeWorker&) = delete;
    DecodeWorker& operator=(const DecodeWorker&) = delete;

    Result<void> submitOpen(const std::filesystem::path& path);
    void submitPlay();
    void submitPause();
    void submitStop();
    Result<void> submitSeek(std::chrono::milliseconds position);

private:
    enum class CommandType {
        Open,
        Play,
        Pause,
        Stop,
        Seek
    };

    enum class DecodePrerollTarget {
        None,
        Audio,
        Video
    };

    struct Command {
        CommandType type = CommandType::Stop;
        std::filesystem::path path;
        std::chrono::milliseconds position { 0 };
    };

    void run(WorkerStopToken stopToken);
    void submit(Command command);
    bool waitForCommand(WorkerStopToken stopToken, Command& command);
    bool tryTakeCommand(Command& command);
    std::chrono::milliseconds coalescedSeekPosition(std::chrono::milliseconds position);
    void handleCommand(Command command, WorkerStopToken stopToken);
    void handleOpen(const std::filesystem::path& path);
    void decodeUntilBlocked(WorkerStopToken stopToken);
    void decodeSeekPreroll(WorkerStopToken stopToken);
    bool handleSeek(std::chrono::milliseconds position);
    void beginAccurateSeek(std::chrono::milliseconds position);
    void emitSeekCompletedIfReady();
    void closeMedia();

    Result<void> decodePacket(AVCodecContext* codecContext,
                              const AVPacket* packet,
                              AVRational timeBase,
                              bool video,
                              DecodePrerollTarget prerollTarget = DecodePrerollTarget::None,
                              bool* prerollDelivered = nullptr);
    void flushDecoders();
    PlayerEvent makeEvent(PlayerEventPayload payload) const;
    void emitEvent(PlayerEvent event);
    void emitState(PlayerState state);
    void emitError(MediaError error);
    StreamDecoder::FrameHandlerStatus emitVideoFrame(VideoFrame frame);
    DecodeFrameMetadata frameMetadata() const;
    StreamDecoder::FrameHandlerStatus handleFramePushResult(DecodeFramePushResult result);
    void recordFramePushResult(DecodeFramePushResult result);
    AudioFrame makeAudioFrame(AVFramePtr frame) const;
    AudioSampleFormat publishedInterleavedAudioSampleFormat(AVSampleFormat format) const;

    PlayerConfig m_config;
    IEventSink& m_events;
    IDecodeFrameSink& m_frames;
    std::unique_ptr<WorkerThread> m_thread;

    mutable std::mutex m_mutex;
    std::condition_variable_any m_cv;
    std::deque<Command> m_commands;
    bool m_acceptingCommands = true;

    Demuxer m_demuxer;
    StreamDecoder m_streamDecoder;
    VideoFrameProcessor m_videoFrameProcessor;
    DecodePerformanceStats m_decodeStats;
    OpenedMedia m_media;
    std::optional<SeekPrerollGate> m_seekGate;
    std::optional<std::chrono::microseconds> m_pendingSeekTarget;
    std::uint64_t m_sessionId = 0;
    std::uint64_t m_generation = 0;
    bool m_hasMedia = false;
    bool m_playing = false;
};

} // namespace media_sdk
