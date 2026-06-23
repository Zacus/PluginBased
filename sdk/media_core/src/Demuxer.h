#pragma once

#include "FFmpegUtils.h"
#include "media_sdk/MediaEvents.h"
#include "media_sdk/Result.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace media_sdk {

struct OpenedMedia {
    AVFormatContextPtr formatContext;
    AVCodecContextPtr videoCodecContext;
    AVCodecContextPtr audioCodecContext;

    int videoStreamIndex = -1;
    int audioStreamIndex = -1;
    MediaInfo info;
    std::uint64_t audioChannelLayoutMask = 0;
    int audioSampleFormat = AV_SAMPLE_FMT_FLTP;
    std::string activeVideoDecoderName = "software";
};

class Demuxer
{
public:
    Result<OpenedMedia> open(const std::filesystem::path& path) const;

private:
    bool openVideoStream(OpenedMedia& media) const;
    bool openAudioStream(OpenedMedia& media) const;
    AVCodecContext* createCodecContext(AVStream* stream, const AVCodec* codec) const;
};

} // namespace media_sdk
