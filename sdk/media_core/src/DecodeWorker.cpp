#include "DecodeWorker.h"

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

} // namespace

DecodeWorker::DecodeWorker(PlayerConfig config, IEventSink& events)
    : m_config(std::move(config))
    , m_events(events)
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
    while (m_commands.empty() && m_acceptingCommands && !stopToken.stop_requested())
        m_cv.wait_for(lock, std::chrono::milliseconds(10));
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
        handleSeek(coalescedSeekPosition(command.position));
        if (m_playing)
            decodeUntilBlocked(stopToken);
        break;
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
            }
        }

        av_packet_unref(packet.get());
    }
}

void DecodeWorker::handleSeek(std::chrono::milliseconds position)
{
    if (!m_hasMedia)
        return;

    const auto targetUs = std::chrono::duration_cast<std::chrono::microseconds>(position).count();
    const int ret = av_seek_frame(m_media.formatContext.get(), -1, targetUs, AVSEEK_FLAG_BACKWARD);
    if (ret < 0)
    {
        emitError(makeError(MediaErrorCode::SeekFailed,
                            "Failed to seek media",
                            avErrorString(ret)));
        return;
    }

    if (m_media.videoCodecContext)
        avcodec_flush_buffers(m_media.videoCodecContext.get());
    if (m_media.audioCodecContext)
        avcodec_flush_buffers(m_media.audioCodecContext.get());
    ++m_generation;

    emitEvent(makeEvent(SeekCompletedEvent { position }));
    emitEvent(makeEvent(PositionChangedEvent { position }));
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
}

Result<void> DecodeWorker::decodePacket(AVCodecContext* codecContext,
                                        const AVPacket* packet,
                                        AVRational timeBase,
                                        bool video)
{
    return m_streamDecoder.sendPacket(
        codecContext,
        packet,
        timeBase,
        [this, video](AVFramePtr frame) {
            if (video)
            {
                ++m_decodeStats.decodedVideoFrames;
                auto processed = m_videoFrameProcessor.process(
                    std::move(frame),
                    { .preferNativeVideoFrames = m_config.preferNativeVideoFrames },
                    m_media.hardwareDecoder.get(),
                    &m_decodeStats);
                if (!processed.ok())
                {
                    emitError(processed.error());
                    return false;
                }
                return emitVideoFrame(std::move(processed.value()));
            }

            AudioFrame audioFrame = makeAudioFrame(std::move(frame));
            emitEvent(makeEvent(PositionChangedEvent {
                std::chrono::duration_cast<std::chrono::milliseconds>(audioFrame.pts()) }));
            emitEvent(makeEvent(AudioFrameEvent { std::move(audioFrame) }));
            return true;
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
                return processed.ok() && emitVideoFrame(std::move(processed.value()));
            });
    }
    if (m_media.audioCodecContext)
    {
        (void)m_streamDecoder.flush(
            m_media.audioCodecContext.get(),
            m_media.formatContext->streams[m_media.audioStreamIndex]->time_base,
            [this](AVFramePtr frame) {
                AudioFrame audioFrame = makeAudioFrame(std::move(frame));
                emitEvent(makeEvent(AudioFrameEvent { std::move(audioFrame) }));
                return true;
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

bool DecodeWorker::emitVideoFrame(VideoFrame frame)
{
    emitEvent(makeEvent(VideoFrameEvent { std::move(frame) }));
    return true;
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
