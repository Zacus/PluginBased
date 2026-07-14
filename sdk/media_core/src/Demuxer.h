#pragma once

#include "DecoderBufferPool.h"
#include "FFmpegUtils.h"
#include "HardwareDecoderBackend.h"
#include "media_sdk/MediaEvents.h"
#include "media_sdk/Result.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace media_sdk {

struct OpenedMedia {
    OpenedMedia() = default;
    ~OpenedMedia();
    OpenedMedia(const OpenedMedia&) = delete;
    OpenedMedia& operator=(const OpenedMedia&) = delete;
    OpenedMedia(OpenedMedia&& other) noexcept;
    OpenedMedia& operator=(OpenedMedia&& other) noexcept;

    AVFormatContextPtr formatContext;
    AVCodecContextPtr videoCodecContext;
    AVCodecContextPtr audioCodecContext;
    std::unique_ptr<HardwareDecoderBackend> hardwareDecoder;
    std::shared_ptr<DecoderBufferPool> decoderBufferPool;

    int videoStreamIndex = -1;
    int audioStreamIndex = -1;
    MediaInfo info;
    std::uint64_t audioChannelLayoutMask = 0;
    int audioSampleFormat = AV_SAMPLE_FMT_FLTP;
    std::string activeVideoDecoderName = "software";

private:
    void resetVideoDecoder() noexcept;
};

struct DemuxerOptions {
    bool enableHardwareDecode = true;
    bool enableDecoderBufferPool = true;
};

class Demuxer
{
public:
    Result<OpenedMedia> open(const std::filesystem::path& path,
                             DemuxerOptions options = {}) const;

private:
    bool openVideoStream(OpenedMedia& media, const DemuxerOptions& options) const;
    bool openAudioStream(OpenedMedia& media) const;
    AVCodecContext* createCodecContext(AVStream* stream, const AVCodec* codec) const;
};

} // namespace media_sdk
