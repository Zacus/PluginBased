#include "DecodePerformance.h"
#include "Demuxer.h"
#include "FFmpegUtils.h"
#include "StreamDecoder.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace {

void writeLe16(std::ofstream& out, std::uint16_t value)
{
    out.put(static_cast<char>(value & 0xff));
    out.put(static_cast<char>((value >> 8) & 0xff));
}

void writeLe32(std::ofstream& out, std::uint32_t value)
{
    writeLe16(out, static_cast<std::uint16_t>(value & 0xffff));
    writeLe16(out, static_cast<std::uint16_t>((value >> 16) & 0xffff));
}

std::filesystem::path writeTinyWav()
{
    const int sampleRate = 8000;
    const int channels = 1;
    const int bitsPerSample = 16;
    const int frameCount = sampleRate / 4;
    const int dataBytes = frameCount * channels * (bitsPerSample / 8);

    const auto path = std::filesystem::temp_directory_path()
        / ("media_sdk_core_stage4_" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".wav");

    std::ofstream out(path, std::ios::binary);
    assert(out);
    out.write("RIFF", 4);
    writeLe32(out, static_cast<std::uint32_t>(36 + dataBytes));
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeLe32(out, 16);
    writeLe16(out, 1);
    writeLe16(out, channels);
    writeLe32(out, sampleRate);
    writeLe32(out, sampleRate * channels * (bitsPerSample / 8));
    writeLe16(out, channels * (bitsPerSample / 8));
    writeLe16(out, bitsPerSample);
    out.write("data", 4);
    writeLe32(out, static_cast<std::uint32_t>(dataBytes));

    for (int i = 0; i < frameCount; ++i)
    {
        const double phase = static_cast<double>(i) * 440.0 * 2.0 * 3.14159265358979323846
            / static_cast<double>(sampleRate);
        const auto sample = static_cast<std::int16_t>(std::sin(phase) * 12000.0);
        writeLe16(out, static_cast<std::uint16_t>(sample));
    }

    return path;
}

void testDemuxerOpensFileAndStreamDecoderReadsFrame()
{
    const auto samplePath = writeTinyWav();

    media_sdk::Demuxer demuxer;
    auto opened = demuxer.open(samplePath);
    assert(opened.ok());

    auto& media = opened.value();
    assert(media.audioStreamIndex >= 0);
    assert(media.videoStreamIndex < 0);
    assert(media.info.sampleRate == 8000);
    assert(media.info.channels == 1);
    assert(media.info.duration >= 200ms);
    assert(!media.info.formatName.empty());
    assert(media.audioCodecContext);
    assert(media.formatContext);

    media_sdk::StreamDecoder decoder;
    auto packet = media_sdk::makePacket();
    int decodedFrames = 0;
    std::int64_t firstPts = AV_NOPTS_VALUE;

    while (av_read_frame(media.formatContext.get(), packet.get()) >= 0)
    {
        if (packet->stream_index == media.audioStreamIndex)
        {
            const auto result = decoder.sendPacket(
                media.audioCodecContext.get(),
                packet.get(),
                media.formatContext->streams[media.audioStreamIndex]->time_base,
                [&](media_sdk::AVFramePtr frame) {
                    ++decodedFrames;
                    if (firstPts == AV_NOPTS_VALUE)
                        firstPts = frame->pts;
                    return true;
                });
            assert(result.ok());
        }

        av_packet_unref(packet.get());
        if (decodedFrames > 0)
            break;
    }

    const auto flushed = decoder.flush(
        media.audioCodecContext.get(),
        media.formatContext->streams[media.audioStreamIndex]->time_base,
        [&](media_sdk::AVFramePtr frame) {
            ++decodedFrames;
            if (firstPts == AV_NOPTS_VALUE)
                firstPts = frame->pts;
            return true;
        });
    assert(flushed.ok());
    assert(decodedFrames > 0);
    assert(firstPts != AV_NOPTS_VALUE);

    std::filesystem::remove(samplePath);
}

void testDemuxerReportsMissingFile()
{
    media_sdk::Demuxer demuxer;
    const auto opened = demuxer.open(std::filesystem::path("/definitely/not/a/media/file.wav"));
    assert(!opened.ok());
    assert(opened.error().code == media_sdk::MediaErrorCode::OpenFailed);
    assert(!opened.error().message.empty());
}

void testDecodePerformanceCreatesThrottledReportWithoutQt()
{
    media_sdk::DecodePerformanceLogger logger(2s);
    const auto start = std::chrono::steady_clock::time_point {};
    logger.reset(start);
    auto& stats = logger.stats();
    stats.decodedVideoFrames = 4;
    stats.transferredVideoFrames = 2;
    stats.transferTotalUs = 300;
    stats.transferMaxUs = 200;
    stats.normalizedVideoFrames = 3;
    stats.normalizeTotalUs = 600;
    stats.normalizeMaxUs = 250;
    stats.framePushAccepted = 5;
    stats.framePushBackpressured = 2;
    stats.framePushWaitCount = 2;
    stats.framePushWaitUs = 90;
    stats.framePushMaxWaitUs = 60;
    stats.sourcePixelFormat = AV_PIX_FMT_YUV420P;
    stats.cpuPixelFormat = AV_PIX_FMT_RGB24;

    const auto early = logger.maybeCreateReport("software", start + 1500ms);
    assert(!early.has_value());

    const auto report = logger.maybeCreateReport("software", start + 2500ms);
    assert(report.has_value());
    assert(report->decoderName == "software");
    assert(report->stats.decodedVideoFrames == 4);
    assert(report->stats.framePushAccepted == 5);
    assert(report->stats.framePushBackpressured == 2);
    assert(report->stats.framePushWaitCount == 2);
    assert(report->framePushAverageWaitUs == 45);
    assert(report->stats.framePushMaxWaitUs == 60);
    assert(report->transferAverageUs == 150);
    assert(report->normalizeAverageUs == 200);
    assert(report->sourcePixelFormatName == "yuv420p");
    assert(report->cpuPixelFormatName == "rgb24");
    assert(logger.stats().decodedVideoFrames == 0);
    assert(logger.stats().framePushAccepted == 0);
}

void testDecodePerformanceReportsFramePushOnlyActivity()
{
    media_sdk::DecodePerformanceLogger logger(2s);
    const auto start = std::chrono::steady_clock::time_point {};
    logger.reset(start);
    auto& stats = logger.stats();
    stats.framePushAccepted = 2;
    stats.framePushStale = 1;

    const auto report = logger.maybeCreateReport("audio-only", start + 2500ms);
    assert(report.has_value());
    assert(report->decoderName == "audio-only");
    assert(report->stats.decodedVideoFrames == 0);
    assert(report->stats.framePushAccepted == 2);
    assert(report->stats.framePushStale == 1);
}

} // namespace

int main()
{
    testDemuxerOpensFileAndStreamDecoderReadsFrame();
    testDemuxerReportsMissingFile();
    testDecodePerformanceCreatesThrottledReportWithoutQt();
    testDecodePerformanceReportsFramePushOnlyActivity();
    return 0;
}
