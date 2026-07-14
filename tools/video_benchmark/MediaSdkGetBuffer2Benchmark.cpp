#include "ExperimentalDecoderBufferPool.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <sys/utsname.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
}

namespace {

struct Options {
    std::filesystem::path input;
    std::filesystem::path output;
    std::string label = "unlabelled";
    bool prototype = false;
    std::uint64_t maxVideoFrames = 240;
    std::size_t holdVideoFrames = 3;
    std::chrono::milliseconds timeout { 120'000 };
};

struct FormatContextDeleter {
    void operator()(AVFormatContext* context) const
    {
        avformat_close_input(&context);
    }
};

struct CodecContextDeleter {
    void operator()(AVCodecContext* context) const
    {
        avcodec_free_context(&context);
    }
};

struct FrameDeleter {
    void operator()(AVFrame* frame) const
    {
        av_frame_free(&frame);
    }
};

struct PacketDeleter {
    void operator()(AVPacket* packet) const
    {
        av_packet_free(&packet);
    }
};

using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;

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
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            if (argument == "--input") {
                const auto value = nextValue(index, argc, argv);
                if (!value) return std::nullopt;
                options.input = *value;
            } else if (argument == "--output") {
                const auto value = nextValue(index, argc, argv);
                if (!value) return std::nullopt;
                options.output = *value;
            } else if (argument == "--label") {
                const auto value = nextValue(index, argc, argv);
                if (!value) return std::nullopt;
                options.label = *value;
            } else if (argument == "--allocator") {
                const auto value = nextValue(index, argc, argv);
                if (!value || (*value != "default" && *value != "prototype"))
                    return std::nullopt;
                options.prototype = *value == "prototype";
            } else if (argument == "--max-video-frames") {
                const auto value = nextValue(index, argc, argv);
                if (!value) return std::nullopt;
                options.maxVideoFrames = std::stoull(*value);
            } else if (argument == "--hold-video-frames") {
                const auto value = nextValue(index, argc, argv);
                if (!value) return std::nullopt;
                options.holdVideoFrames = std::stoull(*value);
            } else if (argument == "--timeout-ms") {
                const auto value = nextValue(index, argc, argv);
                if (!value) return std::nullopt;
                options.timeout = std::chrono::milliseconds(std::stoll(*value));
            } else {
                return std::nullopt;
            }
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (options.input.empty() || options.output.empty() || options.maxVideoFrames == 0)
        return std::nullopt;
    return options;
}

std::string jsonEscape(std::string_view value)
{
    std::ostringstream escaped;
    for (const unsigned char character : value) {
        switch (character) {
        case '"': escaped << "\\\""; break;
        case '\\': escaped << "\\\\"; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default: escaped << static_cast<char>(character); break;
        }
    }
    return escaped.str();
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

std::string systemName()
{
    utsname information {};
    if (uname(&information) != 0)
        return "unknown";
    return std::string(information.sysname) + " " + information.release + " " + information.machine;
}

std::string ffmpegError(int error)
{
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer {};
    av_strerror(error, buffer.data(), buffer.size());
    return buffer.data();
}

struct DecodeResult {
    std::uint64_t frames = 0;
    std::uint64_t checksum = 1'469'598'103'934'665'603ULL;
    std::map<std::string, std::uint64_t> formats;
    std::deque<FramePtr> heldFrames;
};

bool consumeFrames(
    AVCodecContext* context,
    AVFrame* frame,
    const Options& options,
    DecodeResult& result)
{
    while (result.frames < options.maxVideoFrames) {
        const int receiveResult = avcodec_receive_frame(context, frame);
        if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF)
            return true;
        if (receiveResult < 0) {
            std::cerr << "Receive frame failed: " << ffmpegError(receiveResult) << '\n';
            return false;
        }

        ++result.frames;
        const auto format = static_cast<AVPixelFormat>(frame->format);
        const char* formatName = av_get_pix_fmt_name(format);
        ++result.formats[formatName ? formatName : "unknown"];
        for (int plane = 0; plane < AV_NUM_DATA_POINTERS; ++plane) {
            if (!frame->data[plane] || frame->linesize[plane] == 0)
                continue;
            result.checksum ^= frame->data[plane][0];
            result.checksum *= 1'099'511'628'211ULL;
        }
        if (options.holdVideoFrames > 0) {
            AVFrame* clone = av_frame_clone(frame);
            if (!clone)
                return false;
            result.heldFrames.emplace_back(clone);
            while (result.heldFrames.size() > options.holdVideoFrames)
                result.heldFrames.pop_front();
        }
        av_frame_unref(frame);
    }
    return true;
}

void writeFormats(std::ostream& output, const std::map<std::string, std::uint64_t>& formats)
{
    output << '{';
    bool first = true;
    for (const auto& [name, count] : formats) {
        if (!first) output << ',';
        first = false;
        output << '"' << jsonEscape(name) << "\":" << count;
    }
    output << '}';
}

} // namespace

int main(int argc, char** argv)
{
    const auto options = parseOptions(argc, argv);
    if (!options) {
        std::cerr << "Usage: MediaSdkGetBuffer2Benchmark --input FILE --output FILE --label NAME "
                     "--allocator default|prototype [--max-video-frames N] "
                     "[--hold-video-frames N] [--timeout-ms N]\n";
        return 2;
    }

    AVFormatContext* rawFormat = nullptr;
    int status = avformat_open_input(&rawFormat, options->input.string().c_str(), nullptr, nullptr);
    if (status < 0) {
        std::cerr << "Open input failed: " << ffmpegError(status) << '\n';
        return 1;
    }
    FormatContextPtr format(rawFormat);
    if ((status = avformat_find_stream_info(format.get(), nullptr)) < 0) {
        std::cerr << "Find stream info failed: " << ffmpegError(status) << '\n';
        return 1;
    }
    const int streamIndex = av_find_best_stream(
        format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0) {
        std::cerr << "No video stream\n";
        return 1;
    }
    AVStream* stream = format->streams[streamIndex];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        std::cerr << "No video decoder\n";
        return 1;
    }

    CodecContextPtr context(avcodec_alloc_context3(codec));
    if (!context || avcodec_parameters_to_context(context.get(), stream->codecpar) < 0)
        return 1;
    context->thread_count = 0;

    media_benchmark::ExperimentalDecoderBufferPool pool(options->prototype);
    if (!pool.attach(context.get()))
        return 1;
    if ((status = avcodec_open2(context.get(), codec, nullptr)) < 0) {
        pool.detach(context.get());
        std::cerr << "Open decoder failed: " << ffmpegError(status) << '\n';
        return 1;
    }

    PacketPtr packet(av_packet_alloc());
    FramePtr frame(av_frame_alloc());
    if (!packet || !frame)
        return 1;

    rusage usageBefore {};
    getrusage(RUSAGE_SELF, &usageBefore);
    const auto started = std::chrono::steady_clock::now();
    DecodeResult decoded;
    bool decodeSucceeded = true;
    while (decoded.frames < options->maxVideoFrames
           && std::chrono::steady_clock::now() - started < options->timeout) {
        const int readResult = av_read_frame(format.get(), packet.get());
        if (readResult < 0)
            break;
        if (packet->stream_index == streamIndex) {
            status = avcodec_send_packet(context.get(), packet.get());
            if (status < 0 && status != AVERROR(EAGAIN)) {
                decodeSucceeded = false;
                break;
            }
            if (!consumeFrames(context.get(), frame.get(), *options, decoded)) {
                decodeSucceeded = false;
                break;
            }
        }
        av_packet_unref(packet.get());
    }
    if (decodeSucceeded && decoded.frames < options->maxVideoFrames) {
        avcodec_send_packet(context.get(), nullptr);
        decodeSucceeded = consumeFrames(context.get(), frame.get(), *options, decoded);
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    rusage usageAfter {};
    getrusage(RUSAGE_SELF, &usageAfter);

    const auto allocatorStats = pool.stats();
    const std::string codecName = codec->name ? codec->name : "unknown";
    const int threadCount = context->thread_count;
    const int width = context->width;
    const int height = context->height;
    pool.detach(context.get());
    context.reset();
    pool.close();
    decoded.heldFrames.clear();

    const bool completed = decodeSucceeded && decoded.frames == options->maxVideoFrames;
    std::filesystem::create_directories(options->output.parent_path());
    std::ofstream output(options->output);
    if (!output)
        return 1;
    const double wallMs = std::chrono::duration<double, std::milli>(elapsed).count();
    const double userMs = (timevalSeconds(usageAfter.ru_utime) - timevalSeconds(usageBefore.ru_utime)) * 1000.0;
    const double systemMs = (timevalSeconds(usageAfter.ru_stime) - timevalSeconds(usageBefore.ru_stime)) * 1000.0;
    output << std::fixed << std::setprecision(3)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"benchmark_kind\": \"get_buffer2_f1\",\n"
           << "  \"label\": \"" << jsonEscape(options->label) << "\",\n"
           << "  \"allocator_mode\": \"" << (options->prototype ? "prototype" : "default") << "\",\n"
           << "  \"input\": \"" << jsonEscape(std::filesystem::absolute(options->input).string()) << "\",\n"
           << "  \"system\": \"" << jsonEscape(systemName()) << "\",\n"
           << "  \"completed\": " << (completed ? "true" : "false") << ",\n"
           << "  \"media\": {\"codec\":\"" << jsonEscape(codecName)
           << "\",\"width\":" << width << ",\"height\":" << height
           << ",\"thread_count\":" << threadCount << "},\n"
           << "  \"frames\": {\"video\":" << decoded.frames
           << ",\"checksum\":" << decoded.checksum << ",\"pixel_formats\":";
    writeFormats(output, decoded.formats);
    output << "},\n"
           << "  \"timing\": {\"wall_ms\":" << wallMs
           << ",\"user_cpu_ms\":" << userMs
           << ",\"system_cpu_ms\":" << systemMs
           << ",\"max_rss_bytes\":" << maxRssBytes(usageAfter) << "},\n"
           << "  \"allocator\": {\"callback_count\":" << allocatorStats.callbackCount
           << ",\"prototype_frame_count\":" << allocatorStats.prototypeFrameCount
           << ",\"fallback_count\":" << allocatorStats.fallbackCount
           << ",\"pool_rebuild_count\":" << allocatorStats.poolRebuildCount
           << ",\"plane_acquire_count\":" << allocatorStats.planeAcquireCount
           << ",\"plane_allocation_count\":" << allocatorStats.planeAllocationCount << "}\n"
           << "}\n";
    if (!completed) {
        std::cerr << "Benchmark stopped after " << decoded.frames << " frames\n";
        return 1;
    }
    return 0;
}
