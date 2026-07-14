extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

struct Options {
    std::filesystem::path output;
    AVCodecID codecId = AV_CODEC_ID_H264;
    std::string encoderName;
    std::string encoderProfile;
    AVPixelFormat pixelFormat = AV_PIX_FMT_NONE;
    int width = 1920;
    int height = 1080;
    int fps = 30;
    int seconds = 6;
    int bitRate = 8'000'000;
};

struct CodecContextDeleter {
    void operator()(AVCodecContext* value) const { avcodec_free_context(&value); }
};

struct FrameDeleter {
    void operator()(AVFrame* value) const { av_frame_free(&value); }
};

struct PacketDeleter {
    void operator()(AVPacket* value) const { av_packet_free(&value); }
};

std::string avError(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof(buffer));
    return buffer;
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
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        const auto readInteger = [&](int& target) -> bool {
            const auto value = nextValue(i, argc, argv);
            if (!value) return false;
            target = std::stoi(*value);
            return true;
        };
        if (argument == "--output") {
            const auto value = nextValue(i, argc, argv);
            if (!value) return std::nullopt;
            options.output = *value;
        } else if (argument == "--codec") {
            const auto value = nextValue(i, argc, argv);
            if (!value) return std::nullopt;
            if (*value == "h264") options.codecId = AV_CODEC_ID_H264;
            else if (*value == "hevc") options.codecId = AV_CODEC_ID_HEVC;
            else if (*value == "mpeg4") options.codecId = AV_CODEC_ID_MPEG4;
            else if (*value == "prores") {
                options.codecId = AV_CODEC_ID_PRORES;
                options.encoderName = "prores_ks";
                options.encoderProfile = "standard";
                options.pixelFormat = AV_PIX_FMT_YUV422P10LE;
            }
            else return std::nullopt;
        } else if (argument == "--encoder") {
            const auto value = nextValue(i, argc, argv);
            if (!value) return std::nullopt;
            options.encoderName = *value;
        } else if (argument == "--pixel-format") {
            const auto value = nextValue(i, argc, argv);
            if (!value) return std::nullopt;
            options.pixelFormat = av_get_pix_fmt(value->c_str());
            if (options.pixelFormat == AV_PIX_FMT_NONE) return std::nullopt;
        } else if (argument == "--profile") {
            const auto value = nextValue(i, argc, argv);
            if (!value) return std::nullopt;
            options.encoderProfile = *value;
        } else if (argument == "--width") {
            if (!readInteger(options.width)) return std::nullopt;
        } else if (argument == "--height") {
            if (!readInteger(options.height)) return std::nullopt;
        } else if (argument == "--fps") {
            if (!readInteger(options.fps)) return std::nullopt;
        } else if (argument == "--seconds") {
            if (!readInteger(options.seconds)) return std::nullopt;
        } else if (argument == "--bitrate") {
            if (!readInteger(options.bitRate)) return std::nullopt;
        } else {
            return std::nullopt;
        }
    }
    if (options.output.empty() || options.width <= 0 || options.height <= 0
        || options.fps <= 0 || options.seconds <= 0 || options.bitRate <= 0) {
        return std::nullopt;
    }
    return options;
}

AVPixelFormat selectPixelFormat(const AVCodec* codec, AVPixelFormat requested)
{
    const void* configurations = nullptr;
    int configurationCount = 0;
    if (avcodec_get_supported_config(nullptr,
                                     codec,
                                     AV_CODEC_CONFIG_PIX_FORMAT,
                                     0,
                                     &configurations,
                                     &configurationCount) < 0
        || !configurations || configurationCount <= 0) {
        return requested != AV_PIX_FMT_NONE ? requested : AV_PIX_FMT_YUV420P;
    }

    const auto* formats = static_cast<const AVPixelFormat*>(configurations);
    if (requested != AV_PIX_FMT_NONE) {
        for (int i = 0; i < configurationCount; ++i) {
            if (formats[i] == requested)
                return formats[i];
        }
        return AV_PIX_FMT_NONE;
    }
    for (int i = 0; i < configurationCount; ++i) {
        if (formats[i] == AV_PIX_FMT_YUV420P)
            return formats[i];
    }
    for (int i = 0; i < configurationCount; ++i) {
        if (formats[i] == AV_PIX_FMT_NV12)
            return formats[i];
    }
    return formats[0];
}

void fillYuv420p(AVFrame* frame, int frameIndex)
{
    for (int y = 0; y < frame->height; ++y) {
        auto* row = frame->data[0] + y * frame->linesize[0];
        for (int x = 0; x < frame->width; ++x)
            row[x] = static_cast<std::uint8_t>((x + y + frameIndex * 3) & 0xff);
    }
    for (int y = 0; y < frame->height / 2; ++y) {
        auto* u = frame->data[1] + y * frame->linesize[1];
        auto* v = frame->data[2] + y * frame->linesize[2];
        for (int x = 0; x < frame->width / 2; ++x) {
            u[x] = static_cast<std::uint8_t>(96 + ((x + frameIndex) & 31));
            v[x] = static_cast<std::uint8_t>(160 - ((y + frameIndex) & 31));
        }
    }
}

void fillNv12(AVFrame* frame, int frameIndex)
{
    for (int y = 0; y < frame->height; ++y) {
        auto* row = frame->data[0] + y * frame->linesize[0];
        for (int x = 0; x < frame->width; ++x)
            row[x] = static_cast<std::uint8_t>((x + y + frameIndex * 3) & 0xff);
    }
    for (int y = 0; y < frame->height / 2; ++y) {
        auto* uv = frame->data[1] + y * frame->linesize[1];
        for (int x = 0; x < frame->width; x += 2) {
            uv[x] = static_cast<std::uint8_t>(96 + ((x / 2 + frameIndex) & 31));
            uv[x + 1] = static_cast<std::uint8_t>(160 - ((y + frameIndex) & 31));
        }
    }
}

void fillYuv422p10le(AVFrame* frame, int frameIndex)
{
    for (int y = 0; y < frame->height; ++y) {
        auto* row = reinterpret_cast<std::uint16_t*>(frame->data[0] + y * frame->linesize[0]);
        for (int x = 0; x < frame->width; ++x)
            row[x] = static_cast<std::uint16_t>((x + y + frameIndex * 3) & 0x3ff);
    }
    for (int y = 0; y < frame->height; ++y) {
        auto* u = reinterpret_cast<std::uint16_t*>(frame->data[1] + y * frame->linesize[1]);
        auto* v = reinterpret_cast<std::uint16_t*>(frame->data[2] + y * frame->linesize[2]);
        for (int x = 0; x < frame->width / 2; ++x) {
            u[x] = static_cast<std::uint16_t>(384 + ((x + frameIndex) & 0x7f));
            v[x] = static_cast<std::uint16_t>(640 - ((y + frameIndex) & 0x7f));
        }
    }
}

int drainPackets(AVCodecContext* codecContext,
                 AVFormatContext* formatContext,
                 AVStream* stream)
{
    std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
    if (!packet)
        return AVERROR(ENOMEM);

    while (true) {
        const int receiveResult = avcodec_receive_packet(codecContext, packet.get());
        if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF)
            return 0;
        if (receiveResult < 0)
            return receiveResult;
        av_packet_rescale_ts(packet.get(), codecContext->time_base, stream->time_base);
        packet->stream_index = stream->index;
        const int writeResult = av_interleaved_write_frame(formatContext, packet.get());
        av_packet_unref(packet.get());
        if (writeResult < 0)
            return writeResult;
    }
}

} // namespace

int main(int argc, char** argv)
{
    const auto options = parseOptions(argc, argv);
    if (!options) {
        std::cerr << "Usage: MediaSdkBenchmarkMediaGenerator --output FILE "
                     "--codec h264|hevc|mpeg4|prores [--encoder NAME] "
                     "[--pixel-format NAME] [--profile NAME] --width N --height N --fps N --seconds N "
                     "--bitrate N\n";
        return 2;
    }

    const AVCodec* codec = options->encoderName.empty()
        ? avcodec_find_encoder(options->codecId)
        : avcodec_find_encoder_by_name(options->encoderName.c_str());
    if (!codec) {
        std::cerr << "No encoder is available for " << avcodec_get_name(options->codecId) << '\n';
        return 1;
    }

    std::filesystem::create_directories(options->output.parent_path());
    AVFormatContext* rawFormatContext = nullptr;
    int result = avformat_alloc_output_context2(
        &rawFormatContext, nullptr, nullptr, options->output.string().c_str());
    if (result < 0 || !rawFormatContext) {
        std::cerr << "Cannot create output context: " << avError(result) << '\n';
        return 1;
    }
    std::unique_ptr<AVFormatContext, decltype(&avformat_free_context)> formatContext(
        rawFormatContext, avformat_free_context);
    std::unique_ptr<AVCodecContext, CodecContextDeleter> codecContext(avcodec_alloc_context3(codec));
    if (!codecContext) {
        std::cerr << "Cannot allocate encoder context\n";
        return 1;
    }

    codecContext->codec_id = options->codecId;
    codecContext->width = options->width;
    codecContext->height = options->height;
    codecContext->time_base = { 1, options->fps };
    codecContext->framerate = { options->fps, 1 };
    codecContext->bit_rate = options->bitRate;
    codecContext->gop_size = options->fps * 2;
    codecContext->max_b_frames = 0;
    codecContext->pix_fmt = selectPixelFormat(codec, options->pixelFormat);
    if (codecContext->pix_fmt == AV_PIX_FMT_NONE) {
        std::cerr << "Encoder " << codec->name << " does not support the requested pixel format\n";
        return 1;
    }
    if (codecContext->pix_fmt == AV_PIX_FMT_YUV422P10LE)
        codecContext->bits_per_raw_sample = 10;
    if (options->codecId == AV_CODEC_ID_PRORES)
        codecContext->profile = AV_PROFILE_PRORES_STANDARD;
    if (formatContext->oformat->flags & AVFMT_GLOBALHEADER)
        codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    av_opt_set(codecContext->priv_data, "preset", "fast", 0);
    av_opt_set(codecContext->priv_data, "realtime", "1", 0);
    av_opt_set(codecContext->priv_data, "allow_sw", "1", 0);
    if (!options->encoderProfile.empty())
        av_opt_set(codecContext->priv_data, "profile", options->encoderProfile.c_str(), 0);

    result = avcodec_open2(codecContext.get(), codec, nullptr);
    if (result < 0) {
        std::cerr << "Cannot open encoder " << codec->name << ": " << avError(result) << '\n';
        return 1;
    }
    AVStream* stream = avformat_new_stream(formatContext.get(), nullptr);
    if (!stream) {
        std::cerr << "Cannot create output stream\n";
        return 1;
    }
    stream->time_base = codecContext->time_base;
    result = avcodec_parameters_from_context(stream->codecpar, codecContext.get());
    if (result < 0) {
        std::cerr << "Cannot copy encoder parameters: " << avError(result) << '\n';
        return 1;
    }

    if (!(formatContext->oformat->flags & AVFMT_NOFILE)) {
        result = avio_open(&formatContext->pb, options->output.string().c_str(), AVIO_FLAG_WRITE);
        if (result < 0) {
            std::cerr << "Cannot open output file: " << avError(result) << '\n';
            return 1;
        }
    }
    result = avformat_write_header(formatContext.get(), nullptr);
    if (result < 0) {
        std::cerr << "Cannot write header: " << avError(result) << '\n';
        return 1;
    }

    std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
    frame->format = codecContext->pix_fmt;
    frame->width = codecContext->width;
    frame->height = codecContext->height;
    result = av_frame_get_buffer(frame.get(), 32);
    if (result < 0) {
        std::cerr << "Cannot allocate frame buffer: " << avError(result) << '\n';
        return 1;
    }

    const int frameCount = options->fps * options->seconds;
    for (int i = 0; i < frameCount; ++i) {
        result = av_frame_make_writable(frame.get());
        if (result < 0)
            break;
        if (frame->format == AV_PIX_FMT_YUV420P)
            fillYuv420p(frame.get(), i);
        else if (frame->format == AV_PIX_FMT_NV12)
            fillNv12(frame.get(), i);
        else if (frame->format == AV_PIX_FMT_YUV422P10LE)
            fillYuv422p10le(frame.get(), i);
        else {
            std::cerr << "Unsupported encoder pixel format: " << frame->format << '\n';
            return 1;
        }
        frame->pts = i;
        result = avcodec_send_frame(codecContext.get(), frame.get());
        if (result < 0)
            break;
        result = drainPackets(codecContext.get(), formatContext.get(), stream);
        if (result < 0)
            break;
    }
    if (result >= 0) {
        result = avcodec_send_frame(codecContext.get(), nullptr);
        if (result >= 0)
            result = drainPackets(codecContext.get(), formatContext.get(), stream);
    }
    if (result >= 0)
        result = av_write_trailer(formatContext.get());
    if (formatContext->pb)
        avio_closep(&formatContext->pb);
    if (result < 0) {
        std::cerr << "Encoding failed: " << avError(result) << '\n';
        return 1;
    }

    std::cout << "Generated " << options->output << " using " << codec->name
              << " (" << options->width << 'x' << options->height << '@' << options->fps
              << ", " << frameCount << " frames)\n";
    return 0;
}
