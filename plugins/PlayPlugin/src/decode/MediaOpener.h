#pragma once

// 打开 FFmpeg 媒体输入，并发现可解码的视频/音频流。
// 打开成功后，返回的原生资源所有权交给 FFmpegDecoder；MediaOpener 本身没有线程亲和性。

#include "FFmpegUtils.h"
#include "hw/HardwareDecoderBackend.h"

#include <QString>
#include <QtGlobal>

#include <memory>

struct OpenedMedia
{
    AVFormatContextPtr formatContext;
    AVCodecContextPtr videoCodecContext;
    AVCodecContextPtr audioCodecContext;
    std::unique_ptr<HardwareDecoderBackend> hardwareDecoder;

    int videoStreamIndex = -1;
    int audioStreamIndex = -1;
    qint64 durationMs = 0;
    int videoWidth = 0;
    int videoHeight = 0;
    double videoFps = 0.0;
    int audioChannels = 0;
    int audioSampleRate = 0;
    quint64 audioChannelLayoutMask = 0;
    int audioSampleFormat = AV_SAMPLE_FMT_FLTP;
    QString formatName;
    QString activeVideoDecoderName = QStringLiteral("software");
};

struct MediaOpenResult
{
    bool ok = false;
    QString errorMessage;
    OpenedMedia media;
};

class MediaOpener
{
public:
    MediaOpenResult open(const QString& path);

private:
    bool openVideoStream(OpenedMedia& media);
    bool openAudioStream(OpenedMedia& media);
    bool openVideoCodec(OpenedMedia& media, AVStream* stream, const AVCodec* codec);
    AVCodecContext* createVideoCodecContext(AVStream* stream, const AVCodec* codec) const;
};
