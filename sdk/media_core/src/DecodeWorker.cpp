#include "DecodeWorker.h"

#include "SeekAudioTrimmer.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace media_sdk {
namespace {

MediaError makeError(MediaErrorCode code, std::string message, std::string detail = {})
{
    return { code, std::move(message), std::move(detail) };
}

std::vector<std::byte> makeInterleavedAudioSamples(const AVFrame* frame, AVSampleFormat format)
{
    if (!frame)
        return {};

    const int bytesPerSample = av_get_bytes_per_sample(format);
    const int channels = frame->ch_layout.nb_channels;
    if (bytesPerSample <= 0 || channels <= 0 || frame->nb_samples <= 0)
        return {};

    const auto totalBytes = static_cast<std::size_t>(frame->nb_samples)
        * static_cast<std::size_t>(channels)
        * static_cast<std::size_t>(bytesPerSample);
    std::vector<std::byte> samples(totalBytes);

    if (av_sample_fmt_is_planar(format) != 0)
    {
        auto* out = samples.data();
        for (int sample = 0; sample < frame->nb_samples; ++sample)
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto* planeData = reinterpret_cast<const std::byte*>(
                    frame->extended_data ? frame->extended_data[channel] : nullptr);
                if (!planeData)
                    return {};

                std::memcpy(out,
                            planeData + static_cast<std::size_t>(sample) * bytesPerSample,
                            static_cast<std::size_t>(bytesPerSample));
                out += bytesPerSample;
            }
        }
        return samples;
    }

    const auto* begin = reinterpret_cast<const std::byte*>(frame->extended_data
        ? frame->extended_data[0]
        : nullptr);
    if (!begin)
        return {};

    std::memcpy(samples.data(), begin, totalBytes);
    return samples;
}

bool isDeliveredFramePush(DecodeFramePushResult result)
{
    return result.status == DecodeFramePushStatus::Accepted
        || result.status == DecodeFramePushStatus::Backpressured;
}

} // namespace

DecodeWorker::DecodeWorker(PlayerConfig config, IEventSink& events, IDecodeFrameSink& frames)
    : m_config(std::move(config))
    , m_events(events)
    , m_frames(frames)
{
    m_thread = std::make_unique<WorkerThread>([this](WorkerStopToken stopToken) {
        run(stopToken);
    });
}

DecodeWorker::~DecodeWorker()
{
    {
        std::scoped_lock lock(m_mutex);
        m_acceptingCommands = false;
        m_commands.push_back({ .type = CommandType::Stop });
    }
    m_cv.notify_all();
    if (m_thread && m_thread->joinable())
        m_thread->request_stop();
    m_thread.reset();
}

Result<void> DecodeWorker::submitOpen(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return Result<void>::failure(makeError(MediaErrorCode::OpenFailed,
                                              "Cannot open an empty media path"));
    }

    submit({ .type = CommandType::Open, .path = path });
    return Result<void>::success();
}

void DecodeWorker::submitPlay()
{
    submit({ .type = CommandType::Play });
}

void DecodeWorker::submitPause()
{
    submit({ .type = CommandType::Pause });
}

void DecodeWorker::submitStop()
{
    submit({ .type = CommandType::Stop });
}

Result<void> DecodeWorker::submitSeek(std::chrono::milliseconds position)
{
    if (position < std::chrono::milliseconds { 0 })
    {
        return Result<void>::failure(makeError(MediaErrorCode::SeekFailed,
                                              "Cannot seek to a negative position"));
    }

    submit({ .type = CommandType::Seek, .position = position });
    return Result<void>::success();
}

void DecodeWorker::run(WorkerStopToken stopToken)
{
    while (!stopToken.stop_requested())
    {
        Command command;
        if (!waitForCommand(stopToken, command))
            break;
        handleCommand(std::move(command), stopToken);
    }

    closeMedia();
}

void DecodeWorker::submit(Command command)
{
    {
        std::scoped_lock lock(m_mutex);
        if (!m_acceptingCommands)
            return;
        m_commands.push_back(std::move(command));
    }
    m_cv.notify_all();
}

bool DecodeWorker::waitForCommand(WorkerStopToken stopToken, Command& command)
{
    std::unique_lock lock(m_mutex);
    m_cv.wait(lock, [this, stopToken]() {
        return !m_commands.empty() || !m_acceptingCommands || stopToken.stop_requested();
    });
    if (stopToken.stop_requested() || m_commands.empty())
        return false;

    command = std::move(m_commands.front());
    m_commands.pop_front();
    return true;
}

bool DecodeWorker::tryTakeCommand(Command& command)
{
    std::scoped_lock lock(m_mutex);
    if (m_commands.empty())
        return false;

    command = std::move(m_commands.front());
    m_commands.pop_front();
    return true;
}

std::chrono::milliseconds DecodeWorker::coalescedSeekPosition(std::chrono::milliseconds position)
{
    std::scoped_lock lock(m_mutex);
    while (!m_commands.empty() && m_commands.front().type == CommandType::Seek)
    {
        position = m_commands.front().position;
        m_commands.pop_front();
    }
    return position;
}

void DecodeWorker::handleCommand(Command command, WorkerStopToken stopToken)
{
    switch (command.type)
    {
    case CommandType::Open:
        handleOpen(command.path);
        break;
    case CommandType::Play:
        if (m_hasMedia)
        {
            m_playing = true;
            emitState(PlayerState::Playing);
            decodeUntilBlocked(stopToken);
        }
        break;
    case CommandType::Pause:
        m_playing = false;
        emitState(PlayerState::Paused);
        break;
    case CommandType::Stop:
        closeMedia();
        emitState(PlayerState::Stopped);
        break;
    case CommandType::Seek:
    {
        const bool wasPlaying = m_playing;
        if (!handleSeek(coalescedSeekPosition(command.position)))
            break;
        if (wasPlaying)
            decodeUntilBlocked(stopToken);
        else
            decodeSeekPreroll(stopToken);
        break;
    }
    }
}

void DecodeWorker::handleOpen(const std::filesystem::path& path)
{
    closeMedia();
    auto opened = m_demuxer.open(path, {
        .enableHardwareDecode = m_config.enableHardwareDecode,
    });
    if (!opened.ok())
    {
        emitError(opened.error());
        emitState(PlayerState::Error);
        return;
    }

    m_media = std::move(opened.value());
    ++m_sessionId;
    m_generation = 0;
    m_hasMedia = true;
    m_playing = false;

    emitEvent(makeEvent(MediaInfoEvent { m_media.info }));
    emitState(PlayerState::Paused);
}

void DecodeWorker::decodeUntilBlocked(WorkerStopToken stopToken)
{
    if (!m_hasMedia)
        return;

    auto packet = makePacket();

    while (!stopToken.stop_requested() && m_hasMedia && m_playing)
    {
        Command command;
        if (tryTakeCommand(command))
        {
            handleCommand(std::move(command), stopToken);
            if (!m_hasMedia || !m_playing)
                return;
        }

        const int ret = av_read_frame(m_media.formatContext.get(), packet.get());
        if (ret == AVERROR_EOF)
        {
            flushDecoders();
            emitEvent(makeEvent(EndOfFileEvent {}));
            m_playing = false;
            emitState(PlayerState::Finished);
            return;
        }
        if (ret < 0)
        {
            emitError(makeError(MediaErrorCode::DecodeFailed,
                                "Failed to read media packet",
                                avErrorString(ret)));
            m_playing = false;
            emitState(PlayerState::Error);
            return;
        }

        if (packet->stream_index == m_media.videoStreamIndex && m_media.videoCodecContext)
        {
            const auto result = decodePacket(m_media.videoCodecContext.get(),
                                             packet.get(),
                                             m_media.formatContext->streams[m_media.videoStreamIndex]->time_base,
                                             true);
            if (!result.ok())
            {
                emitError(result.error());
                m_playing = false;
                emitState(PlayerState::Error);
                return;
            }
        }
        else if (packet->stream_index == m_media.audioStreamIndex && m_media.audioCodecContext)
        {
            const auto result = decodePacket(m_media.audioCodecContext.get(),
                                             packet.get(),
                                             m_media.formatContext->streams[m_media.audioStreamIndex]->time_base,
                                             false);
            if (!result.ok())
            {
                emitError(result.error());
                m_playing = false;
                emitState(PlayerState::Error);
                return;
            }
        }

        av_packet_unref(packet.get());
    }
}

void DecodeWorker::decodeSeekPreroll(WorkerStopToken stopToken)
{
    if (!m_hasMedia)
        return;

    const bool targetVideo = m_media.videoStreamIndex >= 0 && m_media.videoCodecContext;
    const bool targetAudio = !targetVideo && m_media.audioStreamIndex >= 0 && m_media.audioCodecContext;
    if (!targetVideo && !targetAudio)
        return;

    bool delivered = false;
    auto packet = makePacket();

    while (!stopToken.stop_requested() && m_hasMedia && !m_playing && !delivered)
    {
        Command command;
        if (tryTakeCommand(command))
        {
            handleCommand(std::move(command), stopToken);
            return;
        }

        const int ret = av_read_frame(m_media.formatContext.get(), packet.get());
        if (ret == AVERROR_EOF)
            return;
        if (ret < 0)
        {
            emitError(makeError(MediaErrorCode::DecodeFailed,
                                "Failed to read media packet",
                                avErrorString(ret)));
            m_playing = false;
            emitState(PlayerState::Error);
            return;
        }

        if (targetVideo
            && packet->stream_index == m_media.videoStreamIndex
            && m_media.videoCodecContext)
        {
            const auto result = decodePacket(m_media.videoCodecContext.get(),
                                             packet.get(),
                                             m_media.formatContext->streams[m_media.videoStreamIndex]->time_base,
                                             true,
                                             targetVideo ? DecodePrerollTarget::Video : DecodePrerollTarget::None,
                                             &delivered);
            if (!result.ok())
            {
                emitError(result.error());
                m_playing = false;
                emitState(PlayerState::Error);
            }
        }
        else if (targetAudio
                 && packet->stream_index == m_media.audioStreamIndex
                 && m_media.audioCodecContext)
        {
            const auto result = decodePacket(m_media.audioCodecContext.get(),
                                             packet.get(),
                                             m_media.formatContext->streams[m_media.audioStreamIndex]->time_base,
                                             false,
                                             targetAudio ? DecodePrerollTarget::Audio : DecodePrerollTarget::None,
                                             &delivered);
            if (!result.ok())
            {
                emitError(result.error());
                m_playing = false;
                emitState(PlayerState::Error);
            }
        }

        av_packet_unref(packet.get());
    }
}

bool DecodeWorker::handleSeek(std::chrono::milliseconds position)
{
    if (!m_hasMedia)
        return false;

    const auto targetUs = std::chrono::duration_cast<std::chrono::microseconds>(position).count();
    const int ret = av_seek_frame(m_media.formatContext.get(), -1, targetUs, AVSEEK_FLAG_BACKWARD);
    if (ret < 0)
    {
        emitError(makeError(MediaErrorCode::SeekFailed,
                            "Failed to seek media",
                            avErrorString(ret)));
        return false;
    }

    if (m_media.videoCodecContext)
        avcodec_flush_buffers(m_media.videoCodecContext.get());
    if (m_media.audioCodecContext)
        avcodec_flush_buffers(m_media.audioCodecContext.get());
    ++m_generation;

    beginAccurateSeek(position);
    return true;
}

void DecodeWorker::beginAccurateSeek(std::chrono::milliseconds position)
{
    const auto target = std::chrono::duration_cast<std::chrono::microseconds>(position);
    m_pendingSeekTarget = target;
    m_seekGate.emplace(SeekPrerollGateConfig {
        .target = target,
        .generation = m_generation,
        .hasVideo = m_media.videoStreamIndex >= 0 && m_media.videoCodecContext,
        .hasAudio = m_media.audioStreamIndex >= 0 && m_media.audioCodecContext,
    });
}

void DecodeWorker::emitSeekCompletedIfReady()
{
    if (!m_seekGate || !m_seekGate->shouldEmitCompletion())
        return;

    // seek 完成事件代表目标侧媒体已经进入交付路径，而不是 av_seek_frame 已返回。
    const auto completedTarget = m_pendingSeekTarget.value_or(m_seekGate->completionPosition());
    const auto position = std::chrono::duration_cast<std::chrono::milliseconds>(completedTarget);
    emitEvent(makeEvent(SeekCompletedEvent { position }));
    emitEvent(makeEvent(PositionChangedEvent { position }));
    m_seekGate->markCompletionSent();
    m_seekGate.reset();
    m_pendingSeekTarget.reset();
}

void DecodeWorker::closeMedia()
{
    if (!m_hasMedia)
        return;

    m_videoFrameProcessor.reset();
    m_media = {};
    m_hasMedia = false;
    m_playing = false;
    m_decodeStats = {};
    m_seekGate.reset();
    m_pendingSeekTarget.reset();
}

Result<void> DecodeWorker::decodePacket(AVCodecContext* codecContext,
                                        const AVPacket* packet,
                                        AVRational timeBase,
                                        bool video,
                                        DecodePrerollTarget prerollTarget,
                                        bool* prerollDelivered)
{
    return m_streamDecoder.sendPacket(
        codecContext,
        packet,
        timeBase,
        [this, video, prerollTarget, prerollDelivered](AVFramePtr frame) {
            if (video)
            {
                ++m_decodeStats.decodedVideoFrames;
                if (m_seekGate)
                {
                    // 精确 seek 预滚期间，目标点前的视频帧不能进入像素处理和 runtime 队列。
                    const auto decision = frame->pts == AV_NOPTS_VALUE
                        ? SeekPrerollDecision {
                            .action = m_seekGate->completionPosition() <= std::chrono::microseconds { 0 }
                                ? SeekPrerollAction::Accept
                                : SeekPrerollAction::Discard
                        }
                        : m_seekGate->inspectVideo(std::chrono::microseconds { frame->pts }, m_generation);
                    if (decision.action == SeekPrerollAction::Discard
                        || decision.action == SeekPrerollAction::Stale)
                        return StreamDecoder::FrameHandlerStatus::Continue;
                }
                auto processed = m_videoFrameProcessor.process(
                    std::move(frame),
                    { .preferNativeVideoFrames = m_config.preferNativeVideoFrames },
                    m_media.hardwareDecoder.get(),
                    &m_decodeStats);
                if (!processed.ok())
                {
                    emitError(processed.error());
                    return StreamDecoder::FrameHandlerStatus::Reject;
                }
                auto pushResult = m_frames.pushVideo(std::move(processed.value()), frameMetadata());
                const auto status = handleFramePushResult(pushResult);
                if (prerollTarget == DecodePrerollTarget::Video && isDeliveredFramePush(pushResult))
                {
                    if (prerollDelivered)
                        *prerollDelivered = true;
                    if (m_seekGate)
                    {
                        m_seekGate->markVideoAccepted();
                        emitSeekCompletedIfReady();
                    }
                    return StreamDecoder::FrameHandlerStatus::Stop;
                }
                if (m_seekGate && isDeliveredFramePush(pushResult))
                {
                    m_seekGate->markVideoAccepted();
                    emitSeekCompletedIfReady();
                }
                return status;
            }

            AudioFrame audioFrame = makeAudioFrame(std::move(frame));
            const bool pendingAudioSeek = m_seekGate && m_pendingSeekTarget.has_value();
            if (pendingAudioSeek)
            {
                // seek 预滚期间先裁剪/丢弃目标前 samples，避免提前污染 position 和 runtime audio queue。
                auto trimResult = trimAudioFrameForSeek(audioFrame, *m_pendingSeekTarget);
                if (trimResult.status == SeekAudioTrimStatus::Discard)
                    return StreamDecoder::FrameHandlerStatus::Continue;
                if (trimResult.status == SeekAudioTrimStatus::Invalid)
                {
                    emitError(makeError(MediaErrorCode::DecodeFailed,
                                        "Failed to trim audio for accurate seek"));
                    return StreamDecoder::FrameHandlerStatus::Reject;
                }
                audioFrame = std::move(trimResult.frame);
            }
            else
            {
                emitEvent(makeEvent(PositionChangedEvent {
                    std::chrono::duration_cast<std::chrono::milliseconds>(audioFrame.pts()) }));
            }
            auto pushResult = m_frames.pushAudio(std::move(audioFrame), frameMetadata());
            const auto status = handleFramePushResult(pushResult);
            if (pendingAudioSeek && isDeliveredFramePush(pushResult) && m_seekGate)
            {
                m_seekGate->markAudioAccepted();
                emitSeekCompletedIfReady();
            }
            if (prerollTarget == DecodePrerollTarget::Audio && isDeliveredFramePush(pushResult))
            {
                if (prerollDelivered)
                    *prerollDelivered = true;
                return StreamDecoder::FrameHandlerStatus::Stop;
            }
            return status;
        });
}

void DecodeWorker::flushDecoders()
{
    if (m_media.videoCodecContext)
    {
        (void)m_streamDecoder.flush(
            m_media.videoCodecContext.get(),
            m_media.formatContext->streams[m_media.videoStreamIndex]->time_base,
            [this](AVFramePtr frame) {
                auto processed = m_videoFrameProcessor.process(
                    std::move(frame),
                    { .preferNativeVideoFrames = m_config.preferNativeVideoFrames },
                    m_media.hardwareDecoder.get(),
                    &m_decodeStats);
                if (!processed.ok())
                {
                    emitError(processed.error());
                    return StreamDecoder::FrameHandlerStatus::Reject;
                }
                return emitVideoFrame(std::move(processed.value()));
            });
    }
    if (m_media.audioCodecContext)
    {
        (void)m_streamDecoder.flush(
            m_media.audioCodecContext.get(),
            m_media.formatContext->streams[m_media.audioStreamIndex]->time_base,
            [this](AVFramePtr frame) {
                AudioFrame audioFrame = makeAudioFrame(std::move(frame));
                return handleFramePushResult(m_frames.pushAudio(std::move(audioFrame), frameMetadata()));
            });
    }
}

PlayerEvent DecodeWorker::makeEvent(PlayerEventPayload payload) const
{
    return {
        .metadata = { .sessionId = m_sessionId, .generation = m_generation },
        .payload = std::move(payload),
    };
}

void DecodeWorker::emitEvent(PlayerEvent event)
{
    m_events.onEvent(event);
}

void DecodeWorker::emitState(PlayerState state)
{
    emitEvent(makeEvent(StateChangedEvent { state }));
}

void DecodeWorker::emitError(MediaError error)
{
    emitEvent(makeEvent(ErrorEvent { std::move(error) }));
}

StreamDecoder::FrameHandlerStatus DecodeWorker::emitVideoFrame(VideoFrame frame)
{
    return handleFramePushResult(m_frames.pushVideo(std::move(frame), frameMetadata()));
}

DecodeFrameMetadata DecodeWorker::frameMetadata() const
{
    return {
        .sessionId = m_sessionId,
        .generation = m_generation,
    };
}

StreamDecoder::FrameHandlerStatus DecodeWorker::handleFramePushResult(DecodeFramePushResult result)
{
    recordFramePushResult(result);

    switch (result.status)
    {
    case DecodeFramePushStatus::Accepted:
    case DecodeFramePushStatus::Backpressured:
    case DecodeFramePushStatus::StaleGeneration:
        return StreamDecoder::FrameHandlerStatus::Continue;
    case DecodeFramePushStatus::Cancelled:
        m_playing = false;
        return StreamDecoder::FrameHandlerStatus::Stop;
    case DecodeFramePushStatus::Closed:
        return StreamDecoder::FrameHandlerStatus::Reject;
    }
    return StreamDecoder::FrameHandlerStatus::Reject;
}

void DecodeWorker::recordFramePushResult(DecodeFramePushResult result)
{
    const auto waitUs = result.waitTime.count();
    if (waitUs > 0)
        ++m_decodeStats.framePushWaitCount;
    m_decodeStats.framePushWaitUs += waitUs;
    m_decodeStats.framePushMaxWaitUs = std::max(m_decodeStats.framePushMaxWaitUs, waitUs);

    switch (result.status)
    {
    case DecodeFramePushStatus::Accepted:
        ++m_decodeStats.framePushAccepted;
        break;
    case DecodeFramePushStatus::Backpressured:
        ++m_decodeStats.framePushBackpressured;
        break;
    case DecodeFramePushStatus::StaleGeneration:
        ++m_decodeStats.framePushStale;
        break;
    case DecodeFramePushStatus::Cancelled:
        ++m_decodeStats.framePushCancelled;
        break;
    case DecodeFramePushStatus::Closed:
        ++m_decodeStats.framePushClosed;
        break;
    }
}

AudioFrame DecodeWorker::makeAudioFrame(AVFramePtr frame) const
{
    const auto format = static_cast<AVSampleFormat>(frame->format);
    const int channels = frame->ch_layout.nb_channels;
    auto samples = makeInterleavedAudioSamples(frame.get(), format);
    // Planar FFmpeg formats are published as interleaved SDK audio frames after
    // makeInterleavedAudioSamples() has reordered the per-channel planes.
    return AudioFrame::fromOwnedSamples(
        publishedInterleavedAudioSampleFormat(format),
        frame->sample_rate,
        channels,
        frame->pts == AV_NOPTS_VALUE
            ? std::chrono::microseconds { 0 }
            : std::chrono::microseconds { frame->pts },
        std::move(samples));
}

AudioSampleFormat DecodeWorker::publishedInterleavedAudioSampleFormat(AVSampleFormat format) const
{
    switch (format)
    {
    case AV_SAMPLE_FMT_FLT:
    case AV_SAMPLE_FMT_FLTP:
        return AudioSampleFormat::Float32Interleaved;
    case AV_SAMPLE_FMT_S16:
    case AV_SAMPLE_FMT_S16P:
        return AudioSampleFormat::Signed16Interleaved;
    case AV_SAMPLE_FMT_S32:
    case AV_SAMPLE_FMT_S32P:
        return AudioSampleFormat::Signed32Interleaved;
    default:
        return AudioSampleFormat::Unknown;
    }
}

} // namespace media_sdk
