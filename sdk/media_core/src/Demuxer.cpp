#include "Demuxer.h"

#include <chrono>
#include <utility>

namespace media_sdk {
namespace {

Result<OpenedMedia> failure(MediaErrorCode code, std::string message, std::string detail = {})
{
    return Result<OpenedMedia>::failure({ code, std::move(message), std::move(detail) });
}

Result<OpenedMedia> success(OpenedMedia media)
{
    return Result<OpenedMedia>::success(std::move(media));
}

} // namespace

Result<OpenedMedia> Demuxer::open(const std::filesystem::path& path) const
{
    OpenedMedia media;
    AVFormatContext* rawFormatContext = nullptr;
    const auto pathString = path.string();
    int ret = avformat_open_input(&rawFormatContext, pathString.c_str(), nullptr, nullptr);
    if (ret < 0)
    {
        return failure(MediaErrorCode::OpenFailed,
                       "Failed to open media input",
                       avErrorString(ret));
    }
    media.formatContext.reset(rawFormatContext);

    ret = avformat_find_stream_info(media.formatContext.get(), nullptr);
    if (ret < 0)
    {
        return failure(MediaErrorCode::StreamInfoFailed,
                       "Failed to read media stream information",
                       avErrorString(ret));
    }

    media.info.duration = media.formatContext->duration != AV_NOPTS_VALUE
        ? std::chrono::milliseconds(media.formatContext->duration / 1000)
        : std::chrono::milliseconds { 0 };
    media.info.formatName = media.formatContext->iformat && media.formatContext->iformat->long_name
        ? media.formatContext->iformat->long_name
        : "unknown";

    openVideoStream(media);
    openAudioStream(media);

    if (media.videoStreamIndex < 0 && media.audioStreamIndex < 0)
    {
        return failure(MediaErrorCode::UnsupportedFormat,
                       "No decodable audio or video stream found");
    }

    return success(std::move(media));
}

bool Demuxer::openVideoStream(OpenedMedia& media) const
{
    media.videoStreamIndex =
        av_find_best_stream(media.formatContext.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (media.videoStreamIndex < 0)
        return false;

    AVStream* stream = media.formatContext->streams[media.videoStreamIndex];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
    {
        media.videoStreamIndex = -1;
        return false;
    }

    AVCodecContext* context = createCodecContext(stream, codec);
    if (!context)
    {
        media.videoStreamIndex = -1;
        return false;
    }

    const int ret = avcodec_open2(context, codec, nullptr);
    if (ret < 0)
    {
        avcodec_free_context(&context);
        media.videoStreamIndex = -1;
        return false;
    }

    media.videoCodecContext.reset(context);
    media.info.width = context->width;
    media.info.height = context->height;
    const AVRational frameRate = stream->avg_frame_rate;
    media.info.fps = frameRate.den > 0 ? av_q2d(frameRate) : 0.0;
    media.activeVideoDecoderName = "software";
    return true;
}

bool Demuxer::openAudioStream(OpenedMedia& media) const
{
    media.audioStreamIndex =
        av_find_best_stream(media.formatContext.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (media.audioStreamIndex < 0)
        return false;

    AVStream* stream = media.formatContext->streams[media.audioStreamIndex];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
    {
        media.audioStreamIndex = -1;
        return false;
    }

    AVCodecContext* context = createCodecContext(stream, codec);
    if (!context)
    {
        media.audioStreamIndex = -1;
        return false;
    }

    const int ret = avcodec_open2(context, codec, nullptr);
    if (ret < 0)
    {
        avcodec_free_context(&context);
        media.audioStreamIndex = -1;
        return false;
    }

    media.audioCodecContext.reset(context);
    media.info.channels = context->ch_layout.nb_channels;
    media.info.sampleRate = context->sample_rate;
    media.audioChannelLayoutMask =
        context->ch_layout.order == AV_CHANNEL_ORDER_NATIVE
            ? static_cast<std::uint64_t>(context->ch_layout.u.mask)
            : 0;
    media.audioSampleFormat = static_cast<int>(context->sample_fmt);
    return true;
}

AVCodecContext* Demuxer::createCodecContext(AVStream* stream, const AVCodec* codec) const
{
    AVCodecContext* context = avcodec_alloc_context3(codec);
    if (!context)
        return nullptr;

    const int ret = avcodec_parameters_to_context(context, stream->codecpar);
    if (ret < 0)
    {
        avcodec_free_context(&context);
        return nullptr;
    }

    context->thread_count = 0;
    return context;
}

} // namespace media_sdk
