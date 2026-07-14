#include "DecodeWorker.h"

#include "SeekAudioTrimmer.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
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

int preferredSeekStreamIndex(const OpenedMedia& media)
{
    if (media.videoStreamIndex >= 0 && media.videoCodecContext)
        return media.videoStreamIndex;
    if (media.audioStreamIndex >= 0 && media.audioCodecContext)
        return media.audioStreamIndex;
    return -1;
}

std::optional<std::chrono::milliseconds> toMilliseconds(
    std::optional<std::chrono::microseconds> value)
{
    if (!value.has_value())
        return std::nullopt;
    return std::chrono::duration_cast<std::chrono::milliseconds>(*value);
}

} // namespace

DecodeWorker::DecodeWorker(PlayerConfig config, IEventSink& events, IDecodeFrameSink& frames)
    : m_config(std::move(config))
    , m_events(events)
    , m_frames(frames)
    , m_decodePerformance(m_config.decodePerformanceReportInterval)
{
    m_decodePerformance.reset();
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

PlayerDiagnostics DecodeWorker::diagnostics() const
{
    const auto stats = m_videoFrameProcessor.picturePoolStats();
    const auto decoderPool = std::atomic_load(&m_decoderBufferPoolDiagnostics);
    const auto decoderStats = decoderPool
        ? decoderPool->stats()
        : DecoderBufferPoolStats {};
    return {
        .videoPicturePool = {
            .acquireCount = stats.acquireCount,
            .reuseCount = stats.reuseCount,
            .allocationCount = stats.allocationCount,
            .transientAllocationCount = stats.transientAllocationCount,
            .highWatermark = stats.highWatermark,
            .retainedCount = stats.retainedCount,
            .inFlightCount = stats.inFlightCount,
        },
        .decoderBufferPool = {
            .callbackCount = decoderStats.callbackCount,
            .pooledFrameCount = decoderStats.pooledFrameCount,
            .fallbackCount = decoderStats.fallbackCount,
            .poolRebuildCount = decoderStats.poolRebuildCount,
            .planeAcquireCount = decoderStats.planeAcquireCount,
            .planeAllocationCount = decoderStats.planeAllocationCount,
        },
    };
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
    return submitSeek(position, SeekPlaybackMode::PreservePlaybackState);
}

Result<void> DecodeWorker::submitSeek(std::chrono::milliseconds position, SeekPlaybackMode mode)
{
    return submitSeek(position, mode, 0);
}

Result<void> DecodeWorker::submitSeek(std::chrono::milliseconds position,
                                      SeekPlaybackMode mode,
                                      SeekRequestId requestId)
{
    if (position < std::chrono::milliseconds { 0 })
    {
        return Result<void>::failure(makeError(MediaErrorCode::SeekFailed,
                                              "Cannot seek to a negative position"));
    }

    submit({
        .type = CommandType::Seek,
        .position = position,
        .seekPlaybackMode = mode,
        .seekRequestId = requestId,
    });
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

DecodeWorker::Command DecodeWorker::coalescedSeekCommand(Command command)
{
    std::scoped_lock lock(m_mutex);
    while (!m_commands.empty() && m_commands.front().type == CommandType::Seek)
    {
        command = std::move(m_commands.front());
        m_commands.pop_front();
    }
    return command;
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
        command = coalescedSeekCommand(std::move(command));
        // 恢复播放 intent 必须进入同一个 seek 命令，避免独立 Play 命令打断暂停预滚。
        const bool resumeAfterSeek = command.seekPlaybackMode == SeekPlaybackMode::ResumePlayback;
        const bool wasPlaying = m_playing || resumeAfterSeek;
        const auto seekPosition = command.position;
        if (!handleSeek(seekPosition, wasPlaying, command.seekRequestId))
            break;
        if (resumeAfterSeek && !m_playing)
        {
            m_playing = true;
            emitState(PlayerState::Playing);
        }
        if (m_playing)
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
    m_decodePerformance.reset();
    auto opened = m_demuxer.open(path, {
        .enableHardwareDecode = m_config.enableHardwareDecode,
        .enableDecoderBufferPool = m_config.enableDecoderBufferPool,
    });
    if (!opened.ok())
    {
        emitError(opened.error());
        emitState(PlayerState::Error);
        return;
    }

    m_media = std::move(opened.value());
    std::atomic_store(&m_decoderBufferPoolDiagnostics, m_media.decoderBufferPool);
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
            if (m_seekGate)
                emitPendingSeekFallbackCompletion();
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
    {
        emitPendingSeekFallbackCompletion();
        return;
    }

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
        {
            flushDecoders();
            if (m_seekGate)
                emitPendingSeekFallbackCompletion();
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

bool DecodeWorker::handleSeek(std::chrono::milliseconds position,
                              bool wasPlaying,
                              SeekRequestId requestId)
{
    if (!m_hasMedia)
        return false;

    const int ret = seekDemuxer(position);
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

    beginAccurateSeek(position, wasPlaying, requestId);
    return true;
}

int DecodeWorker::seekDemuxer(std::chrono::milliseconds position)
{
    const auto targetUs = std::chrono::duration_cast<std::chrono::microseconds>(position).count();
    const int preferredStream = preferredSeekStreamIndex(m_media);
    int ret = -1;
    if (preferredStream >= 0)
    {
        const AVRational timeBase = m_media.formatContext->streams[preferredStream]->time_base;
        const auto targetTs = av_rescale_q(targetUs, AV_TIME_BASE_Q, timeBase);
        // 专业播放器 seek 应锚定主媒体流，避免复杂容器的全局时间索引落到 EOF 附近。
        ret = avformat_seek_file(m_media.formatContext.get(),
                                 preferredStream,
                                 std::numeric_limits<std::int64_t>::min(),
                                 targetTs,
                                 targetTs,
                                 AVSEEK_FLAG_BACKWARD);
        if (ret < 0)
            ret = av_seek_frame(m_media.formatContext.get(),
                                preferredStream,
                                targetTs,
                                AVSEEK_FLAG_BACKWARD);
    }
    if (ret < 0)
        ret = av_seek_frame(m_media.formatContext.get(), -1, targetUs, AVSEEK_FLAG_BACKWARD);
    return ret;
}

void DecodeWorker::beginAccurateSeek(std::chrono::milliseconds position,
                                     bool preferAudioCompletion,
                                     SeekRequestId requestId)
{
    const auto target = std::chrono::duration_cast<std::chrono::microseconds>(position);
    const bool hasAudio = m_media.audioStreamIndex >= 0 && m_media.audioCodecContext;
    const bool hasVideo = m_media.videoStreamIndex >= 0 && m_media.videoCodecContext;
    m_pendingSeekTarget = target;
    m_pendingSeekRequestId = requestId;
    m_seekTailVideoFrame.reset();
    m_seekGate.emplace(SeekPrerollGateConfig {
        .target = target,
        .generation = m_generation,
        .hasVideo = hasVideo,
        .hasAudio = hasAudio,
        .preferAudioCompletion = preferAudioCompletion && hasAudio,
        .allowVideoTailFallback = preferAudioCompletion && hasVideo,
        .maxDiscardedVideoFrames = m_config.accurateSeekMaxDiscardedVideoFrames,
        .maxDiscardedAudioFrames = m_config.accurateSeekMaxDiscardedAudioFrames,
    });
}

void DecodeWorker::emitSeekCompletedIfReady()
{
    if (!m_seekGate)
        return;

    if (m_seekGate->shouldEmitCompletion())
    {
        // seek 完成事件代表目标侧媒体已经进入交付路径，而不是 av_seek_frame 已返回。
        const auto completedTarget = m_pendingSeekTarget.value_or(m_seekGate->completionPosition());
        const auto requested = std::chrono::duration_cast<std::chrono::milliseconds>(completedTarget);
        const auto firstAudio = toMilliseconds(m_seekGate->firstAudioPts());
        const auto firstVideo = toMilliseconds(m_seekGate->firstVideoPts());
        const bool audioGap = firstAudio.has_value()
            && *firstAudio > requested + std::chrono::milliseconds { 50 };
        emitEvent(makeEvent(SeekCompletedEvent {
            .position = requested,
            .requestedPosition = requested,
            .firstAudioPts = firstAudio,
            .firstVideoPts = firstVideo,
            .exact = true,
            .audioGap = audioGap,
            .requestId = m_pendingSeekRequestId,
        }));
        const auto position = requested;
        emitEvent(makeEvent(PositionChangedEvent { position }));
        m_seekGate->markCompletionSent();
    }

    if (m_seekGate && m_seekGate->readyToRetire())
    {
        m_seekGate.reset();
        m_pendingSeekTarget.reset();
        m_pendingSeekRequestId = 0;
    }
}

void DecodeWorker::emitSeekFallbackCompletion()
{
    if (!m_seekGate)
        return;

    // 丢弃上限只释放 seek 请求等待；gate 继续保留，防止目标前帧绕过精确过滤。
    if (!m_seekGate->completionSent())
    {
        const auto completedTarget = m_pendingSeekTarget.value_or(m_seekGate->completionPosition());
        const auto requested = std::chrono::duration_cast<std::chrono::milliseconds>(completedTarget);
        const auto firstAudio = toMilliseconds(m_seekGate->firstAudioPts());
        const auto firstVideo = toMilliseconds(m_seekGate->firstVideoPts());
        const bool audioGap = firstAudio.has_value()
            && *firstAudio > requested + std::chrono::milliseconds { 50 };
        emitEvent(makeEvent(SeekCompletedEvent {
            .position = requested,
            .requestedPosition = requested,
            .firstAudioPts = firstAudio,
            .firstVideoPts = firstVideo,
            .exact = false,
            .audioGap = audioGap,
            .requestId = m_pendingSeekRequestId,
        }));
        const auto position = requested;
        emitEvent(makeEvent(PositionChangedEvent { position }));
        m_seekGate->markCompletionSent();
    }
}

void DecodeWorker::emitPendingSeekFallbackCompletion()
{
    if (!m_seekGate)
        return;

    // EOF 清理仍然要释放 gate；此时已经没有后续帧会绕过 seek 过滤。
    emitSeekFallbackCompletion();
    publishSeekTailVideoFrameIfAvailable();
    m_seekGate.reset();
    m_pendingSeekTarget.reset();
    m_pendingSeekRequestId = 0;
    m_seekTailVideoFrame.reset();
}

void DecodeWorker::publishSeekTailVideoFrameIfAvailable()
{
    if (!m_seekTailVideoFrame.has_value())
        return;

    // 目标点已经越过视频尾帧时不存在严格意义上的目标侧视频帧；
    // EOF 后交付最近的目标前视频帧，避免 seek 到尾段后画面保持黑屏。
    auto pushResult = m_frames.pushVideo(std::move(*m_seekTailVideoFrame), frameMetadata());
    (void)handleFramePushResult(pushResult);
    m_seekTailVideoFrame.reset();
}

void DecodeWorker::closeMedia()
{
    if (!m_hasMedia) {
        m_pendingSeekRequestId = 0;
        return;
    }

    m_videoFrameProcessor.reset();
    std::atomic_store(
        &m_decoderBufferPoolDiagnostics,
        std::shared_ptr<DecoderBufferPool> {});
    m_media = {};
    m_hasMedia = false;
    m_playing = false;
    m_decodePerformance.reset();
    m_seekGate.reset();
    m_pendingSeekTarget.reset();
    m_pendingSeekRequestId = 0;
    m_seekTailVideoFrame.reset();
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
                return handleDecodedVideoFrame(std::move(frame), prerollTarget, prerollDelivered);
            return handleDecodedAudioFrame(std::move(frame), prerollTarget, prerollDelivered);
        });
}

StreamDecoder::FrameHandlerStatus DecodeWorker::handleDecodedVideoFrame(
    AVFramePtr frame,
    DecodePrerollTarget prerollTarget,
    bool* prerollDelivered)
{
    ++m_decodePerformance.stats().decodedVideoFrames;
    bool seekTargetVideoReady = false;
    std::chrono::microseconds seekTargetVideoPts { 0 };
    if (m_seekGate)
    {
        // 精确 seek 预滚期间，目标点前的视频帧不能进入像素处理和 runtime 队列。
        const bool missingVideoPts = frame->pts == AV_NOPTS_VALUE;
        const auto framePts = missingVideoPts
            ? m_seekGate->completionPosition()
            : std::chrono::microseconds { frame->pts };
        const auto decision = missingVideoPts
            ? m_seekGate->inspectMissingVideoPts(m_generation)
            : m_seekGate->inspectVideo(framePts, m_generation);
        if (decision.action == SeekPrerollAction::Discard)
        {
            m_seekGate->markVideoDiscarded();
            if (!missingVideoPts && m_seekGate->allowsVideoTailFallback())
            {
                // 尾帧候选只用于 EOF fallback；处理失败不能让本来应丢弃的目标前帧中断 seek。
                auto processed = m_videoFrameProcessor.process(
                    std::move(frame),
                    { .preferNativeVideoFrames = m_config.preferNativeVideoFrames },
                    m_media.hardwareDecoder.get(),
                    &m_decodePerformance.stats());
                if (processed.ok())
                    m_seekTailVideoFrame = std::move(processed.value());
            }
            bool acceptMissingPtsFallback = false;
            if (m_seekGate->discardLimitReached())
            {
                // 超过预滚保护边界后先释放 seek completion，但继续过滤直到目标侧帧或真实 EOF。
                emitSeekFallbackCompletion();
                // 无 PTS 媒体无法继续做精确时间比较；达到边界后降级接受一个可渲染帧恢复画面。
                acceptMissingPtsFallback = missingVideoPts;
            }
            if (!acceptMissingPtsFallback)
                return StreamDecoder::FrameHandlerStatus::Continue;
            seekTargetVideoReady = true;
            seekTargetVideoPts = framePts;
        }
        else if (decision.action == SeekPrerollAction::Accept)
        {
            seekTargetVideoReady = true;
            seekTargetVideoPts = framePts;
        }
        else if (decision.action == SeekPrerollAction::Stale)
        {
            return StreamDecoder::FrameHandlerStatus::Continue;
        }
    }
    auto processed = m_videoFrameProcessor.process(
        std::move(frame),
        { .preferNativeVideoFrames = m_config.preferNativeVideoFrames },
        m_media.hardwareDecoder.get(),
        &m_decodePerformance.stats());
    if (!processed.ok())
    {
        emitError(processed.error());
        return StreamDecoder::FrameHandlerStatus::Reject;
    }
    if (seekTargetVideoReady && m_seekGate)
    {
        // completion 必须先于 frame push：session 需要先接受新 generation，runtime 才能接收目标帧。
        m_seekGate->markVideoAccepted(seekTargetVideoPts);
        m_seekTailVideoFrame.reset();
        emitSeekCompletedIfReady();
    }
    auto pushResult = m_frames.pushVideo(std::move(processed.value()), frameMetadata());
    const auto status = handleFramePushResult(pushResult);
    if (prerollTarget == DecodePrerollTarget::Video && isDeliveredFramePush(pushResult))
    {
        if (prerollDelivered)
            *prerollDelivered = true;
        return StreamDecoder::FrameHandlerStatus::Stop;
    }
    return status;
}

StreamDecoder::FrameHandlerStatus DecodeWorker::handleDecodedAudioFrame(
    AVFramePtr frame,
    DecodePrerollTarget prerollTarget,
    bool* prerollDelivered)
{
    AudioFrame audioFrame = makeAudioFrame(std::move(frame));
    const bool pendingAudioSeek = m_seekGate && m_pendingSeekTarget.has_value();
    if (pendingAudioSeek)
    {
        // seek 预滚期间先裁剪/丢弃目标前 samples，避免提前污染 position 和 runtime audio queue。
        auto trimResult = trimAudioFrameForSeek(audioFrame, *m_pendingSeekTarget);
        if (trimResult.status == SeekAudioTrimStatus::Discard)
        {
            if (m_seekGate)
            {
                m_seekGate->markAudioDiscarded();
                if (m_seekGate->discardLimitReached())
                {
                    // 音频连续落在目标前时也先释放 completion，但不能放行目标前 samples。
                    emitSeekFallbackCompletion();
                }
            }
            return StreamDecoder::FrameHandlerStatus::Continue;
        }
        if (trimResult.status == SeekAudioTrimStatus::Invalid)
        {
            emitError(makeError(MediaErrorCode::DecodeFailed,
                                "Failed to trim audio for accurate seek"));
            return StreamDecoder::FrameHandlerStatus::Reject;
        }
        audioFrame = std::move(trimResult.frame);
        if (m_seekGate)
        {
            // completion 必须先于 frame push：session 建立新 generation 映射后，目标音频帧才不会被判旧。
            m_seekGate->markAudioAccepted(audioFrame.pts());
            emitSeekCompletedIfReady();
        }
    }
    else
    {
        emitEvent(makeEvent(PositionChangedEvent {
            std::chrono::duration_cast<std::chrono::milliseconds>(audioFrame.pts()) }));
    }
    auto pushResult = m_frames.pushAudio(std::move(audioFrame), frameMetadata());
    const auto status = handleFramePushResult(pushResult);
    if (prerollTarget == DecodePrerollTarget::Audio && isDeliveredFramePush(pushResult))
    {
        if (prerollDelivered)
            *prerollDelivered = true;
        return StreamDecoder::FrameHandlerStatus::Stop;
    }
    return status;
}

void DecodeWorker::flushDecoders()
{
    if (m_media.videoCodecContext)
    {
        (void)m_streamDecoder.flush(
            m_media.videoCodecContext.get(),
            m_media.formatContext->streams[m_media.videoStreamIndex]->time_base,
            [this](AVFramePtr frame) {
                return handleDecodedVideoFrame(std::move(frame));
            });
    }
    if (m_media.audioCodecContext)
    {
        (void)m_streamDecoder.flush(
            m_media.audioCodecContext.get(),
            m_media.formatContext->streams[m_media.audioStreamIndex]->time_base,
            [this](AVFramePtr frame) {
                return handleDecodedAudioFrame(std::move(frame));
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

void DecodeWorker::maybeEmitDecodePerformanceReport()
{
    std::string_view decoderName = "none";
    if (m_media.videoCodecContext && m_media.videoCodecContext->codec
        && m_media.videoCodecContext->codec->name) {
        decoderName = m_media.videoCodecContext->codec->name;
    }

    auto report = m_decodePerformance.maybeCreateReport(decoderName);
    if (!report.has_value())
        return;

    const auto pool = m_videoFrameProcessor.picturePoolStats();
    const auto decoderPool = m_media.decoderBufferPool
        ? m_media.decoderBufferPool->stats()
        : DecoderBufferPoolStats {};
    emitEvent(makeEvent(DecodePerformanceEvent {
        .decoderName = std::move(report->decoderName),
        .decodedVideoFrames = report->stats.decodedVideoFrames,
        .transferAverageUs = report->transferAverageUs,
        .transferMaxUs = report->stats.transferMaxUs,
        .normalizeAverageUs = report->normalizeAverageUs,
        .normalizeMaxUs = report->stats.normalizeMaxUs,
        .framePushAverageWaitUs = report->framePushAverageWaitUs,
        .framePushMaxWaitUs = report->stats.framePushMaxWaitUs,
        .videoPicturePool = {
            .acquireCount = pool.acquireCount,
            .reuseCount = pool.reuseCount,
            .allocationCount = pool.allocationCount,
            .transientAllocationCount = pool.transientAllocationCount,
            .highWatermark = pool.highWatermark,
            .retainedCount = pool.retainedCount,
            .inFlightCount = pool.inFlightCount,
        },
        .decoderBufferPool = {
            .callbackCount = decoderPool.callbackCount,
            .pooledFrameCount = decoderPool.pooledFrameCount,
            .fallbackCount = decoderPool.fallbackCount,
            .poolRebuildCount = decoderPool.poolRebuildCount,
            .planeAcquireCount = decoderPool.planeAcquireCount,
            .planeAllocationCount = decoderPool.planeAllocationCount,
        },
    }));
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
    maybeEmitDecodePerformanceReport();

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
        ++m_decodePerformance.stats().framePushWaitCount;
    m_decodePerformance.stats().framePushWaitUs += waitUs;
    m_decodePerformance.stats().framePushMaxWaitUs = std::max(
        m_decodePerformance.stats().framePushMaxWaitUs,
        waitUs);

    switch (result.status)
    {
    case DecodeFramePushStatus::Accepted:
        ++m_decodePerformance.stats().framePushAccepted;
        break;
    case DecodeFramePushStatus::Backpressured:
        ++m_decodePerformance.stats().framePushBackpressured;
        break;
    case DecodeFramePushStatus::StaleGeneration:
        ++m_decodePerformance.stats().framePushStale;
        break;
    case DecodeFramePushStatus::Cancelled:
        ++m_decodePerformance.stats().framePushCancelled;
        break;
    case DecodeFramePushStatus::Closed:
        ++m_decodePerformance.stats().framePushClosed;
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
