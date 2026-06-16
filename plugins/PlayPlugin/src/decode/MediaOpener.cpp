#include "decode/MediaOpener.h"

// Implements media open, stream selection, and codec setup.
// The returned OpenedMedia object transfers all native resource ownership to FFmpegDecoder.

#include "Logger.h"
#include "hw/HardwareDecoderFactory.h"

#include <utility>

namespace {

MediaOpenResult failure(const QString& message)
{
    MediaOpenResult result;
    result.errorMessage = message;
    return result;
}

MediaOpenResult success(OpenedMedia media)
{
    MediaOpenResult result;
    result.ok = true;
    result.media = std::move(media);
    return result;
}

} // namespace

MediaOpenResult MediaOpener::open(const QString& path)
{
    LOG_INFO("FFmpegDecoder: opening {}", path.toStdString());

    OpenedMedia media;
    AVFormatContext* fmtRaw = nullptr;
    int ret = avformat_open_input(&fmtRaw, path.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0)
    {
        return failure(
            QString("无法打开文件: %1 (%2)").arg(path).arg(QString::fromStdString(av_err(ret))));
    }
    media.formatContext.reset(fmtRaw);

    ret = avformat_find_stream_info(media.formatContext.get(), nullptr);
    if (ret < 0)
    {
        return failure(QString("无法读取流信息: %1").arg(QString::fromStdString(av_err(ret))));
    }

    media.durationMs = (media.formatContext->duration != AV_NOPTS_VALUE)
        ? (media.formatContext->duration / 1000)
        : 0;
    media.formatName = QString::fromUtf8(
        media.formatContext->iformat ? media.formatContext->iformat->long_name : "unknown");

    openVideoStream(media);
    openAudioStream(media);

    if (media.videoStreamIndex < 0 && media.audioStreamIndex < 0)
        return failure("文件中未找到可解码的视频流或音频流");

    LOG_INFO("FFmpegDecoder: opened OK — duration={}ms video={}x{} @{:.1f}fps audio={}ch@{}Hz",
             media.durationMs,
             media.videoWidth,
             media.videoHeight,
             media.videoFps,
             media.audioChannels,
             media.audioSampleRate);
    return success(std::move(media));
}

bool MediaOpener::openVideoStream(OpenedMedia& media)
{
    media.videoStreamIndex =
        av_find_best_stream(media.formatContext.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (media.videoStreamIndex < 0)
        return false;

    AVStream* stream = media.formatContext->streams[media.videoStreamIndex];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
    {
        LOG_WARN("FFmpegDecoder: no video decoder for codec_id={}",
                 static_cast<int>(stream->codecpar->codec_id));
        media.videoStreamIndex = -1;
        return false;
    }

    if (!openVideoCodec(media, stream, codec))
    {
        media.videoStreamIndex = -1;
        return false;
    }
    return true;
}

AVCodecContext* MediaOpener::createVideoCodecContext(AVStream* stream, const AVCodec* codec) const
{
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx)
        return nullptr;

    const int ret = avcodec_parameters_to_context(ctx, stream->codecpar);
    if (ret < 0)
    {
        LOG_WARN("FFmpegDecoder: avcodec_parameters_to_context for video failed: {}",
                 av_err(ret));
        avcodec_free_context(&ctx);
        return nullptr;
    }

    ctx->thread_count = 0;
    return ctx;
}

bool MediaOpener::openVideoCodec(OpenedMedia& media, AVStream* stream, const AVCodec* codec)
{
    AVCodecContext* vctx = createVideoCodecContext(stream, codec);
    if (!vctx)
        return false;

    media.hardwareDecoder = createHardwareDecoderBackend(codec, stream->codecpar->codec_id);
    if (media.hardwareDecoder)
    {
        LOG_INFO("FFmpegDecoder: selected hardware decoder {}",
                 media.hardwareDecoder->name().toStdString());
        if (!media.hardwareDecoder->configureContext(vctx))
        {
            LOG_WARN("FFmpegDecoder: hardware setup failed, fallback to software decoding");
            media.hardwareDecoder->reset();
            media.hardwareDecoder.reset();
        }
    }

    int ret = avcodec_open2(vctx, codec, nullptr);
    if (ret < 0 && media.hardwareDecoder)
    {
        LOG_WARN("FFmpegDecoder: hardware codec open failed: {}, fallback to software decoding",
                 av_err(ret));
        avcodec_free_context(&vctx);
        media.hardwareDecoder->reset();
        media.hardwareDecoder.reset();

        vctx = createVideoCodecContext(stream, codec);
        if (!vctx)
            return false;
        ret = avcodec_open2(vctx, codec, nullptr);
    }

    if (ret < 0)
    {
        avcodec_free_context(&vctx);
        LOG_WARN("FFmpegDecoder: failed to open video decoder: {}", av_err(ret));
        return false;
    }

    media.activeVideoDecoderName = media.hardwareDecoder
        ? media.hardwareDecoder->name()
        : QStringLiteral("software");
    media.videoCodecContext.reset(vctx);
    media.videoWidth = vctx->width;
    media.videoHeight = vctx->height;
    AVRational fr = stream->avg_frame_rate;
    media.videoFps = (fr.den > 0) ? av_q2d(fr) : 25.0;
    return true;
}

bool MediaOpener::openAudioStream(OpenedMedia& media)
{
    media.audioStreamIndex =
        av_find_best_stream(media.formatContext.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (media.audioStreamIndex < 0)
        return false;

    AVStream* stream = media.formatContext->streams[media.audioStreamIndex];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
    {
        LOG_WARN("FFmpegDecoder: no audio decoder for codec_id={}",
                 static_cast<int>(stream->codecpar->codec_id));
        media.audioStreamIndex = -1;
        return false;
    }

    AVCodecContext* actx = avcodec_alloc_context3(codec);
    if (!actx)
    {
        media.audioStreamIndex = -1;
        return false;
    }

    int ret = avcodec_parameters_to_context(actx, stream->codecpar);
    if (ret < 0)
    {
        avcodec_free_context(&actx);
        LOG_WARN("FFmpegDecoder: avcodec_parameters_to_context for audio failed: {}",
                 av_err(ret));
        media.audioStreamIndex = -1;
        return false;
    }

    ret = avcodec_open2(actx, codec, nullptr);
    if (ret < 0)
    {
        avcodec_free_context(&actx);
        LOG_WARN("FFmpegDecoder: failed to open audio decoder: {}", av_err(ret));
        media.audioStreamIndex = -1;
        return false;
    }

    media.audioCodecContext.reset(actx);
    media.audioChannels = actx->ch_layout.nb_channels;
    media.audioSampleRate = actx->sample_rate;
    media.audioChannelLayoutMask =
        (actx->ch_layout.order == AV_CHANNEL_ORDER_NATIVE)
            ? static_cast<quint64>(actx->ch_layout.u.mask)
            : 0;
    media.audioSampleFormat = static_cast<int>(actx->sample_fmt);
    return true;
}
