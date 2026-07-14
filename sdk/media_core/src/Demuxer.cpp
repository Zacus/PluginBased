#include "Demuxer.h"

#include "HardwareDecoderFactory.h"

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

OpenedMedia::~OpenedMedia()
{
    resetVideoDecoder();
}

OpenedMedia::OpenedMedia(OpenedMedia&& other) noexcept
    : formatContext(std::move(other.formatContext))
    , videoCodecContext(std::move(other.videoCodecContext))
    , audioCodecContext(std::move(other.audioCodecContext))
    , hardwareDecoder(std::move(other.hardwareDecoder))
    , decoderBufferPool(std::move(other.decoderBufferPool))
    , videoStreamIndex(other.videoStreamIndex)
    , audioStreamIndex(other.audioStreamIndex)
    , info(std::move(other.info))
    , audioChannelLayoutMask(other.audioChannelLayoutMask)
    , audioSampleFormat(other.audioSampleFormat)
    , activeVideoDecoderName(std::move(other.activeVideoDecoderName))
{
    other.videoStreamIndex = -1;
    other.audioStreamIndex = -1;
}

OpenedMedia& OpenedMedia::operator=(OpenedMedia&& other) noexcept
{
    if (this == &other)
        return *this;

    resetVideoDecoder();
    formatContext = std::move(other.formatContext);
    videoCodecContext = std::move(other.videoCodecContext);
    audioCodecContext = std::move(other.audioCodecContext);
    hardwareDecoder = std::move(other.hardwareDecoder);
    decoderBufferPool = std::move(other.decoderBufferPool);
    videoStreamIndex = other.videoStreamIndex;
    audioStreamIndex = other.audioStreamIndex;
    info = std::move(other.info);
    audioChannelLayoutMask = other.audioChannelLayoutMask;
    audioSampleFormat = other.audioSampleFormat;
    activeVideoDecoderName = std::move(other.activeVideoDecoderName);
    other.videoStreamIndex = -1;
    other.audioStreamIndex = -1;
    return *this;
}

void OpenedMedia::resetVideoDecoder() noexcept
{
    if (decoderBufferPool) {
        decoderBufferPool->detach(videoCodecContext.get());
        decoderBufferPool->close();
    }
    decoderBufferPool.reset();
    hardwareDecoder.reset();
    videoCodecContext.reset();
}

Result<OpenedMedia> Demuxer::open(const std::filesystem::path& path,
                                  DemuxerOptions options) const
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

    openVideoStream(media, options);
    openAudioStream(media);

    if (media.videoStreamIndex < 0 && media.audioStreamIndex < 0)
    {
        return failure(MediaErrorCode::UnsupportedFormat,
                       "No decodable audio or video stream found");
    }

    return success(std::move(media));
}

bool Demuxer::openVideoStream(OpenedMedia& media, const DemuxerOptions& options) const
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

    auto hardwareDecoder = options.enableHardwareDecode
        ? createHardwareDecoderBackend(codec, stream->codecpar->codec_id)
        : nullptr;
    if (hardwareDecoder && !hardwareDecoder->configureContext(context))
        hardwareDecoder.reset();

    std::shared_ptr<DecoderBufferPool> decoderBufferPool;
    const auto attachDecoderBufferPool = [&] {
        if (!options.enableDecoderBufferPool || hardwareDecoder)
            return;
        auto candidate = std::make_shared<DecoderBufferPool>();
        if (candidate->attach(context))
            decoderBufferPool = std::move(candidate);
    };
    const auto releaseContext = [&] {
        if (decoderBufferPool)
            decoderBufferPool->detach(context);
        decoderBufferPool.reset();
        avcodec_free_context(&context);
    };

    attachDecoderBufferPool();

    int ret = avcodec_open2(context, codec, nullptr);
    if (ret < 0 && hardwareDecoder)
    {
        releaseContext();
        hardwareDecoder.reset();

        context = createCodecContext(stream, codec);
        if (!context)
        {
            media.videoStreamIndex = -1;
            return false;
        }
        attachDecoderBufferPool();
        ret = avcodec_open2(context, codec, nullptr);
    }
    if (ret < 0)
    {
        releaseContext();
        media.videoStreamIndex = -1;
        return false;
    }

    media.videoCodecContext.reset(context);
    media.hardwareDecoder = std::move(hardwareDecoder);
    media.decoderBufferPool = std::move(decoderBufferPool);
    media.info.width = context->width;
    media.info.height = context->height;
    const AVRational frameRate = stream->avg_frame_rate;
    media.info.fps = frameRate.den > 0 ? av_q2d(frameRate) : 0.0;
    media.activeVideoDecoderName = media.hardwareDecoder
        ? std::string(media.hardwareDecoder->name())
        : "software";
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
    media.info.channelLayoutMask = media.audioChannelLayoutMask;
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
