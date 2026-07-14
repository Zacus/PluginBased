#include "media_sdk/audio/ffmpeg/FfmpegAudioTempoProcessor.h"

#include "media_sdk/Error.h"
#include "media_sdk/runtime/PlaybackRate.h"

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
}

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace media_sdk::audio::ffmpeg {
namespace {

using FramePtr = std::unique_ptr<AVFrame, void (*)(AVFrame*)>;

void freeFrame(AVFrame* frame)
{
    av_frame_free(&frame);
}

MediaError tempoError(MediaErrorCode code, std::string message, int ffmpegError = 0)
{
    std::string detail;
    if (ffmpegError < 0) {
        std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer {};
        av_strerror(ffmpegError, buffer.data(), buffer.size());
        detail = buffer.data();
    }
    return {
        .code = code,
        .message = std::move(message),
        .detail = std::move(detail),
    };
}

std::int64_t mediaOffsetForSamples(std::uint64_t samples,
                                   int sampleRate,
                                   std::uint32_t rateMillionths)
{
    const auto rate = static_cast<std::uint64_t>(rateMillionths);
    const auto divisor = static_cast<std::uint64_t>(sampleRate);
    const auto wholeSeconds = samples / divisor;
    const auto remainingSamples = samples % divisor;
    return static_cast<std::int64_t>(
        wholeSeconds * rate + remainingSamples * rate / divisor);
}

} // namespace

struct FfmpegAudioTempoProcessor::Impl {
    ~Impl()
    {
        reset();
    }

    Result<void> configure(const runtime::AudioFormat& requestedFormat, double requestedRate)
    {
        reset();
        if (requestedFormat.sampleRate <= 0 || requestedFormat.channels <= 0
            || requestedFormat.sampleFormat != runtime::AudioSampleFormat::Float32) {
            return Result<void>::failure(tempoError(
                MediaErrorCode::UnsupportedFormat,
                "Audio tempo processor requires interleaved Float32 PCM"));
        }
        if (!runtime::isPlaybackRateSupported(requestedRate)) {
            return Result<void>::failure(tempoError(
                MediaErrorCode::InvalidArgument,
                "Audio tempo processor requires a playback rate between 0.5 and 2.0"));
        }

        format = requestedFormat;
        playbackRate = requestedRate;
        rateMillionths = runtime::playbackRateMillionths(requestedRate);
        if (runtime::playbackRatesEqual(requestedRate, runtime::kDefaultPlaybackRate)) {
            bypass = true;
            configured = true;
            return Result<void>::success();
        }

        graph = avfilter_graph_alloc();
        if (!graph) {
            return Result<void>::failure(tempoError(
                MediaErrorCode::InternalStateError,
                "Failed to allocate FFmpeg audio tempo filter graph"));
        }

        const AVFilter* sourceFilter = avfilter_get_by_name("abuffer");
        const AVFilter* tempoFilter = avfilter_get_by_name("atempo");
        const AVFilter* sinkFilter = avfilter_get_by_name("abuffersink");
        if (!sourceFilter || !tempoFilter || !sinkFilter) {
            reset();
            return Result<void>::failure(tempoError(
                MediaErrorCode::InternalStateError,
                "Required FFmpeg audio filters are unavailable"));
        }

        source = avfilter_graph_alloc_filter(graph, sourceFilter, "tempo_source");
        tempo = avfilter_graph_alloc_filter(graph, tempoFilter, "tempo_filter");
        sink = avfilter_graph_alloc_filter(graph, sinkFilter, "tempo_sink");
        if (!source || !tempo || !sink) {
            reset();
            return Result<void>::failure(tempoError(
                MediaErrorCode::InternalStateError,
                "Failed to allocate FFmpeg audio tempo filter nodes"));
        }

        AVBufferSrcParameters* parameters = av_buffersrc_parameters_alloc();
        if (!parameters) {
            reset();
            return Result<void>::failure(tempoError(
                MediaErrorCode::InternalStateError,
                "Failed to allocate FFmpeg audio source parameters"));
        }
        parameters->format = AV_SAMPLE_FMT_FLT;
        parameters->time_base = { 1, 1000000 };
        parameters->sample_rate = format.sampleRate;
        av_channel_layout_default(&parameters->ch_layout, format.channels);
        const int parameterResult = av_buffersrc_parameters_set(source, parameters);
        av_channel_layout_uninit(&parameters->ch_layout);
        av_free(parameters);
        if (parameterResult < 0) {
            const auto error = tempoError(
                MediaErrorCode::UnsupportedFormat,
                "Failed to configure FFmpeg audio source parameters",
                parameterResult);
            reset();
            return Result<void>::failure(error);
        }

        int result = avfilter_init_str(source, nullptr);
        if (result >= 0)
            result = av_opt_set_double(tempo, "tempo", playbackRate, AV_OPT_SEARCH_CHILDREN);
        if (result >= 0)
            result = avfilter_init_str(tempo, nullptr);
        if (result >= 0)
            result = avfilter_init_str(sink, nullptr);
        if (result >= 0)
            result = avfilter_link(source, 0, tempo, 0);
        if (result >= 0)
            result = avfilter_link(tempo, 0, sink, 0);
        if (result >= 0)
            result = avfilter_graph_config(graph, nullptr);
        if (result < 0) {
            const auto error = tempoError(
                MediaErrorCode::UnsupportedFormat,
                "Failed to configure FFmpeg atempo graph",
                result);
            reset();
            return Result<void>::failure(error);
        }

        AVChannelLayout outputLayout {};
        const int layoutResult = av_buffersink_get_ch_layout(sink, &outputLayout);
        const bool outputMatches = layoutResult >= 0
            && outputLayout.nb_channels == format.channels
            && av_buffersink_get_format(sink) == AV_SAMPLE_FMT_FLT
            && av_buffersink_get_sample_rate(sink) == format.sampleRate;
        av_channel_layout_uninit(&outputLayout);
        if (!outputMatches) {
            reset();
            return Result<void>::failure(tempoError(
                MediaErrorCode::UnsupportedFormat,
                "FFmpeg atempo negotiated an incompatible output format"));
        }

        configured = true;
        return Result<void>::success();
    }

    Result<runtime::AudioTempoOutput> process(runtime::AudioBufferView input)
    {
        if (!configured) {
            return Result<runtime::AudioTempoOutput>::failure(tempoError(
                MediaErrorCode::InternalStateError,
                "Audio tempo processor is not configured"));
        }
        if (drainSubmitted) {
            return Result<runtime::AudioTempoOutput>::failure(tempoError(
                MediaErrorCode::InternalStateError,
                "Audio tempo processor cannot accept input after drain"));
        }
        if (!runtime::playbackRatesEqual(input.playbackRate, playbackRate)) {
            return Result<runtime::AudioTempoOutput>::failure(tempoError(
                MediaErrorCode::InvalidArgument,
                "Audio tempo input rate does not match the configured rate"));
        }

        const auto frameBytes = sizeof(float) * static_cast<std::size_t>(format.channels);
        if (input.bytes.size() % frameBytes != 0) {
            return Result<runtime::AudioTempoOutput>::failure(tempoError(
                MediaErrorCode::UnsupportedFormat,
                "Audio tempo input is not aligned to complete PCM frames"));
        }
        if (input.bytes.empty())
            return Result<runtime::AudioTempoOutput>::success({});

        if (!anchorValid) {
            anchorPts = input.pts;
            anchorValid = true;
        }

        if (bypass) {
            runtime::AudioTempoOutput output;
            output.buffers.push_back({
                .bytes = std::vector<std::byte>(input.bytes.begin(), input.bytes.end()),
                .pts = input.pts,
            });
            return Result<runtime::AudioTempoOutput>::success(std::move(output));
        }

        FramePtr frame(av_frame_alloc(), &freeFrame);
        if (!frame) {
            return Result<runtime::AudioTempoOutput>::failure(tempoError(
                MediaErrorCode::InternalStateError,
                "Failed to allocate FFmpeg audio input frame"));
        }
        frame->format = AV_SAMPLE_FMT_FLT;
        frame->sample_rate = format.sampleRate;
        frame->nb_samples = static_cast<int>(input.bytes.size() / frameBytes);
        frame->pts = input.pts.count();
        av_channel_layout_default(&frame->ch_layout, format.channels);
        int result = av_frame_get_buffer(frame.get(), 0);
        if (result < 0) {
            return Result<runtime::AudioTempoOutput>::failure(tempoError(
                MediaErrorCode::InternalStateError,
                "Failed to allocate FFmpeg audio frame samples",
                result));
        }
        std::memcpy(frame->data[0], input.bytes.data(), input.bytes.size());

        result = av_buffersrc_add_frame_flags(source, frame.get(), 0);
        if (result < 0) {
            return Result<runtime::AudioTempoOutput>::failure(tempoError(
                MediaErrorCode::DecodeFailed,
                "Failed to submit PCM to FFmpeg atempo",
                result));
        }
        return pullAvailableOutput();
    }

    Result<runtime::AudioTempoOutput> drain()
    {
        if (!configured) {
            return Result<runtime::AudioTempoOutput>::failure(tempoError(
                MediaErrorCode::InternalStateError,
                "Audio tempo processor is not configured"));
        }
        if (bypass || drainComplete)
            return Result<runtime::AudioTempoOutput>::success({});

        if (!drainSubmitted) {
            const int result = av_buffersrc_add_frame_flags(source, nullptr, 0);
            if (result < 0) {
                return Result<runtime::AudioTempoOutput>::failure(tempoError(
                    MediaErrorCode::DecodeFailed,
                    "Failed to drain FFmpeg atempo input",
                    result));
            }
            drainSubmitted = true;
        }
        return pullAvailableOutput();
    }

    Result<runtime::AudioTempoOutput> pullAvailableOutput()
    {
        runtime::AudioTempoOutput output;
        while (true) {
            FramePtr frame(av_frame_alloc(), &freeFrame);
            if (!frame) {
                return Result<runtime::AudioTempoOutput>::failure(tempoError(
                    MediaErrorCode::InternalStateError,
                    "Failed to allocate FFmpeg audio output frame"));
            }

            const int result = av_buffersink_get_frame(sink, frame.get());
            if (result == AVERROR(EAGAIN))
                break;
            if (result == AVERROR_EOF) {
                drainComplete = true;
                break;
            }
            if (result < 0) {
                return Result<runtime::AudioTempoOutput>::failure(tempoError(
                    MediaErrorCode::DecodeFailed,
                    "Failed to receive PCM from FFmpeg atempo",
                    result));
            }

            const auto sampleCount = static_cast<std::size_t>(frame->nb_samples);
            const auto byteCount = sampleCount
                * static_cast<std::size_t>(format.channels)
                * sizeof(float);
            runtime::AudioTempoBuffer buffer;
            buffer.bytes.resize(byteCount);
            if (byteCount > 0)
                std::memcpy(buffer.bytes.data(), frame->data[0], byteCount);
            buffer.pts = anchorPts + std::chrono::microseconds {
                mediaOffsetForSamples(outputSamples, format.sampleRate, rateMillionths) };
            outputSamples += sampleCount;
            output.buffers.push_back(std::move(buffer));
        }
        return Result<runtime::AudioTempoOutput>::success(std::move(output));
    }

    void reset() noexcept
    {
        if (graph)
            avfilter_graph_free(&graph);
        source = nullptr;
        tempo = nullptr;
        sink = nullptr;
        format = {};
        playbackRate = runtime::kDefaultPlaybackRate;
        rateMillionths = runtime::kPlaybackRateScale;
        anchorPts = std::chrono::microseconds { 0 };
        outputSamples = 0;
        configured = false;
        bypass = false;
        anchorValid = false;
        drainSubmitted = false;
        drainComplete = false;
    }

    AVFilterGraph* graph = nullptr;
    AVFilterContext* source = nullptr;
    AVFilterContext* tempo = nullptr;
    AVFilterContext* sink = nullptr;
    runtime::AudioFormat format {};
    double playbackRate = runtime::kDefaultPlaybackRate;
    std::uint32_t rateMillionths = runtime::kPlaybackRateScale;
    std::chrono::microseconds anchorPts { 0 };
    std::uint64_t outputSamples = 0;
    bool configured = false;
    bool bypass = false;
    bool anchorValid = false;
    bool drainSubmitted = false;
    bool drainComplete = false;
};

FfmpegAudioTempoProcessor::FfmpegAudioTempoProcessor()
    : m_impl(std::make_unique<Impl>())
{
}

FfmpegAudioTempoProcessor::~FfmpegAudioTempoProcessor() = default;

Result<void> FfmpegAudioTempoProcessor::configure(
    const runtime::AudioFormat& format,
    double playbackRate)
{
    return m_impl->configure(format, playbackRate);
}

Result<runtime::AudioTempoOutput> FfmpegAudioTempoProcessor::process(
    runtime::AudioBufferView input)
{
    return m_impl->process(input);
}

Result<runtime::AudioTempoOutput> FfmpegAudioTempoProcessor::drain()
{
    return m_impl->drain();
}

void FfmpegAudioTempoProcessor::reset() noexcept
{
    m_impl->reset();
}

} // namespace media_sdk::audio::ffmpeg
