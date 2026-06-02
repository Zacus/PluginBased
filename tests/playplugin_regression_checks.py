#!/usr/bin/env python3

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    engine_h = read("plugins/PlayPlugin/src/PlayerEngine.h")
    engine_cpp = read("plugins/PlayPlugin/src/PlayerEngine.cpp")
    ffmpeg_utils_h = read("plugins/PlayPlugin/src/FFmpegUtils.h")
    decoder_h = read("plugins/PlayPlugin/src/FFmpegDecoder.h")
    decoder_cpp = read("plugins/PlayPlugin/src/FFmpegDecoder.cpp")
    hw_backend_h = read("plugins/PlayPlugin/src/hw/HardwareDecoderBackend.h")
    hw_factory_h = read("plugins/PlayPlugin/src/hw/HardwareDecoderFactory.h")
    hw_factory_cpp = read("plugins/PlayPlugin/src/hw/HardwareDecoderFactory.cpp")
    videotoolbox_h = read("plugins/PlayPlugin/src/hw/VideoToolboxBackend.h")
    videotoolbox_cpp = read("plugins/PlayPlugin/src/hw/VideoToolboxBackend.cpp")
    d3d11va_cpp = read("plugins/PlayPlugin/src/hw/D3D11VABackend.cpp")
    vaapi_cpp = read("plugins/PlayPlugin/src/hw/VaapiBackend.cpp")
    cmake = read("plugins/PlayPlugin/CMakeLists.txt")
    audio_cpp = read("plugins/PlayPlugin/src/AudioRenderer.cpp")
    audio_h = read("plugins/PlayPlugin/src/AudioRenderer.h")
    surface_cpp = read("plugins/PlayPlugin/src/FFmpegSurface.cpp")
    renderer_cpp = read("plugins/PlayPlugin/src/VideoRenderer.cpp")
    renderer_h = read("plugins/PlayPlugin/src/VideoRenderer.h")
    control_qml = read("plugins/PlayPlugin/qml/ControlBar.qml")
    playlist_qml = read("plugins/PlayPlugin/qml/PlaylistView.qml")
    playplugin_qml = read("plugins/PlayPlugin/qml/PlayPluginView.qml")
    player_qml = read("plugins/PlayPlugin/qml/PlayerView.qml")
    playplugin_cpp = read("plugins/PlayPlugin/PlayPlugin.cpp")
    context_h = read("plugins/PlayPlugin/src/PlaybackContext.h")

    require("finishMedia()" in engine_h, "PlayerEngine should centralize media completion")
    require("maybeFinishMedia()" in engine_h, "PlayerEngine should wait for active streams to drain")
    require("endOfAudio" in read("plugins/PlayPlugin/src/AudioRenderer.h"),
            "AudioRenderer should report audio queue EOF")
    require("m_mediaFinished" in engine_h, "PlayerEngine should track completed media")
    require("resumeAfterSeek" in engine_cpp,
            "seeking after media completion should resume playback")
    require("stopAllComponents();" not in engine_cpp[engine_cpp.find("void PlayerEngine::finishMedia"):],
            "finished media should keep playback components available for seeking")
    require("m_openCond.wakeOne();" in decoder_cpp[decoder_cpp.find("void FFmpegDecoder::seekTo"):],
            "seek should wake decoder even if it is waiting after EOF")
    require(engine_cpp.count("emit endOfMedia();") == 1,
            "endOfMedia should be emitted from one guarded path")
    require("emit positionChanged(m_position);" in engine_cpp and
            "emit durationChanged(m_duration);" in engine_cpp,
            "stop should notify reset position and duration")
    require("stopAllComponents();" in engine_cpp[engine_cpp.find("void PlayerEngine::onDecoderError"):],
            "decoder errors should stop playback components")

    require("bytesPerSample" in surface_cpp,
            "black placeholder upload should account for R16 byte width")
    require('LOG_DEBUG("VideoRenderer: pts=' not in renderer_cpp,
            "per-frame video sync debug logging should be removed or throttled")
    require('LOG_DEBUG("VideoRenderer: drop frame' not in renderer_cpp,
            "per-frame drop logging should stay out of the render loop")
    require('LOG_DEBUG("FFmpegDecoder: converted video pixel format' not in decoder_cpp,
            "per-frame pixel format conversion logging should stay out of the decode loop")
    require("setAudioClockEnabled" in renderer_h and "m_audioClockEnabled" in renderer_h,
            "VideoRenderer should explicitly support video-only clocking")
    require("m_videoClock.restart()" in renderer_cpp and "m_videoClockBaseUs" in renderer_cpp,
            "video-only playback should establish a local clock from the first rendered frame")
    require("m_videoRenderer->setAudioClockEnabled(m_hasAudio)" in engine_cpp,
            "PlayerEngine should configure VideoRenderer clock mode from detected streams")
    require("m_hasRenderedFrame" in renderer_h and
            "m_consecutiveDroppedFrames" in renderer_h and
            "MaxConsecutiveDropsBeforeRender" in renderer_cpp and
            "m_consecutiveDroppedFrames < MaxConsecutiveDropsBeforeRender" in renderer_cpp,
            "VideoRenderer should bound late-frame drops so slow 4K/60 videos keep updating")
    require("seekCompleted(int generation, int serial)" in decoder_h,
            "decoder should report the frame serial produced after seek")
    require("setAcceptedSerial" in read("plugins/PlayPlugin/src/AudioRenderer.h") and
            "setAcceptedSerial" in renderer_h,
            "audio and video renderers should track the currently accepted frame serial")
    require("m_audioRenderer->setAcceptedSerial(serial)" in engine_cpp and
            "m_videoRenderer->completeSeek(generation, serial)" in engine_cpp,
            "PlayerEngine should apply seek serial to all frame consumers")
    require("entry.serial != m_acceptedSerial" in read("plugins/PlayPlugin/src/AudioRenderer.cpp") and
            "entry.serial != m_acceptedSerial" in renderer_cpp,
            "frame consumers should discard stale frames from older seek serials")
    require("quint64 channelLayoutMask" in decoder_h and
            "m_audioChannelLayoutMask" in decoder_h,
            "decoder should expose the source audio channel layout mask")
    require("actx->ch_layout.u.mask" in decoder_cpp,
            "decoder should capture FFmpeg's native channel layout mask")
    require("m_srcChannelLayoutMask" in audio_h and
            "av_channel_layout_from_mask" in audio_cpp and
            "av_channel_layout_default(&srcLayout, m_srcChannels)" in audio_cpp,
            "AudioRenderer should initialize swresample from the real or default source layout")
    require("setPendingOpenUrl" in context_h and "takePendingOpenUrl" in context_h,
            "PlaybackContext should store a pending host open request")
    require("PlaybackContext::instance().setPendingOpenUrl(url)" in playplugin_cpp,
            "PlayPlugin::open should cache host open requests before PlayerEngine exists")
    require("takePendingOpenUrl" in engine_cpp and "QMetaObject::invokeMethod" in engine_cpp,
            "PlayerEngine should consume pending host open requests after QML construction")
    require("buildColorMatrix" not in surface_cpp and "colorMatrix" not in surface_cpp,
            "FFmpegSurface should not keep obsolete color matrix code or comments")
    ensure_textures = surface_cpp[surface_cpp.find("void ensureTextures"):
                                  surface_cpp.find("void releaseTextures")]
    require(ensure_textures.count("m_material_.paramsDirty") == 1 and
            ensure_textures.count("m_material_.cachedFullRange") == 1 and
            ensure_textures.count("m_material_.cachedBt709") == 1,
            "texture recreation should mark material state dirty once")

    require("import QuickUI.Components 1.0" in control_qml and
            "import QuickUI.Components 1.0" in playlist_qml,
            "QML dependency on QuickUI should be explicit")
    require("property bool playlistOpen: false" in playplugin_qml,
            "playlist drawer should be hidden by default")
    require("drawerWidth" in playplugin_qml and "Behavior on x" in playplugin_qml,
            "playlist should be implemented as an animated right drawer")
    require("playlistToggleRequested" in player_qml and
            "showPlaylistButton" in control_qml,
            "player controls should expose a playlist toggle")
    require("HoverHandler" in playlist_qml and "TapHandler" in playlist_qml and
            "id: delegateMouse" not in playlist_qml,
            "playlist row hover/double-click handling should not cover remove buttons")
    require("normalizeVideoFrame" in decoder_h and "sws_getCachedContext" in decoder_cpp,
            "unsupported video pixel formats should be converted before rendering")
    require("class HardwareDecoderBackend" in hw_backend_h,
            "hardware decoder backend interface should exist")
    require("virtual QString name() const = 0" in hw_backend_h,
            "hardware backend should expose a stable log name")
    require("virtual bool isAvailableForCodec" in hw_backend_h,
            "hardware backend should decide codec availability")
    require("virtual bool configureContext(AVCodecContext* codecContext) = 0" in hw_backend_h,
            "hardware backend should configure AVCodecContext before avcodec_open2")
    require("virtual bool isHardwareFrame(const AVFrame* frame) const = 0" in hw_backend_h,
            "hardware backend should identify frames that need transfer")
    require("virtual AVFramePtr transferToCpuFrame(const AVFrame* frame) = 0" in hw_backend_h,
            "hardware backend should transfer hardware frames to CPU frames")
    require("src/hw/HardwareDecoderBackend.h" in cmake,
            "PlayPlugin target should include hardware backend interface")
    require("createHardwareDecoderBackend" in hw_factory_h and
            "std::unique_ptr<HardwareDecoderBackend>" in hw_factory_h,
            "hardware decoder factory should return an optional backend")
    require("#if defined(Q_OS_APPLE)" in hw_factory_cpp and "VideoToolboxBackend" in hw_factory_cpp,
            "factory should select VideoToolbox only on Apple platforms")
    require("#if defined(Q_OS_WIN)" in hw_factory_cpp and "D3D11VABackend" in hw_factory_cpp,
            "factory should know the Windows skeleton backend")
    require("#if defined(Q_OS_LINUX)" in hw_factory_cpp and "VaapiBackend" in hw_factory_cpp,
            "factory should know the Linux skeleton backend")
    require("return false;" in d3d11va_cpp and "d3d11va" in d3d11va_cpp,
            "D3D11VA backend should be explicitly unavailable in phase 1")
    require("return false;" in vaapi_cpp and "vaapi" in vaapi_cpp,
            "VAAPI backend should be explicitly unavailable in phase 1")
    require("videotoolbox" in videotoolbox_cpp,
            "VideoToolbox backend should expose a stable backend name")
    require('#include "hw/HardwareDecoderFactory.h"' in decoder_cpp,
            "FFmpegDecoder should include the hardware backend factory")
    require("std::unique_ptr<HardwareDecoderBackend> m_hardwareDecoder" in decoder_h,
            "FFmpegDecoder should own the selected hardware backend")
    require("createHardwareDecoderBackend(codec, stream->codecpar->codec_id)" in decoder_cpp,
            "FFmpegDecoder should ask the factory for video hardware decoding")
    require("m_hardwareDecoder.reset();" in decoder_cpp[decoder_cpp.find("void FFmpegDecoder::closeInternal"):],
            "FFmpegDecoder should release hardware backend on close")
    require("Q_OS_APPLE" not in decoder_cpp and
            "Q_OS_WIN" not in decoder_cpp and
            "Q_OS_LINUX" not in decoder_cpp,
            "FFmpegDecoder should not contain platform branching for hardware backend selection")
    require("bool openVideoCodec(AVStream* stream, const AVCodec* codec)" in decoder_h,
            "FFmpegDecoder should open video codec through a retryable helper")
    require("openVideoCodec(vs, vcodec)" in decoder_cpp,
            "openInternal should delegate video codec opening")
    require("configureContext(vctx)" in decoder_cpp,
            "video codec helper should configure hardware before avcodec_open2")
    require("fallback to software decoding" in decoder_cpp,
            "hardware open failure should log software fallback")
    require("AVCodecContext* FFmpegDecoder::createVideoCodecContext" in decoder_cpp and
            "vctx = createVideoCodecContext(stream, codec)" in decoder_cpp,
            "software fallback should rebuild a clean AVCodecContext")
    require("#include <libavutil/hwcontext.h>" in ffmpeg_utils_h,
            "FFmpegUtils should expose FFmpeg hardware context APIs")
    require("AVBufferRefPtr" in ffmpeg_utils_h,
            "FFmpegUtils should provide RAII for AVBufferRef")
    require("AVBufferRefPtr m_deviceContext" in videotoolbox_h,
            "VideoToolbox backend should own the hardware device context")
    require("av_hwdevice_ctx_create" in videotoolbox_cpp and
            "AV_HWDEVICE_TYPE_VIDEOTOOLBOX" in videotoolbox_cpp,
            "VideoToolbox backend should create a VideoToolbox hardware device")
    require("avcodec_get_hw_config" in videotoolbox_cpp and
            "AV_PIX_FMT_VIDEOTOOLBOX" in videotoolbox_cpp,
            "VideoToolbox backend should verify decoder hardware config")
    require("selectVideoToolboxFormat" in videotoolbox_cpp and
            "codecContext->get_format = selectVideoToolboxFormat" in videotoolbox_cpp,
            "VideoToolbox backend should force FFmpeg to choose the hardware pixel format")
    require("codecContext->hw_device_ctx = av_buffer_ref" in videotoolbox_cpp,
            "VideoToolbox backend should attach hardware device to AVCodecContext")
    require("av_hwframe_transfer_data" in videotoolbox_cpp,
            "VideoToolbox backend should transfer hardware frames to CPU frames")
    require("copyFrameMetadata" in decoder_cpp,
            "FFmpegDecoder should preserve timing and color metadata after hardware transfer")
    require("prepareVideoFrameForQueue" in decoder_h and "prepareVideoFrameForQueue(std::move(frame))" in decoder_cpp,
            "FFmpegDecoder should prepare video frames through a single helper before queueing")
    prepare_body = decoder_cpp[decoder_cpp.find("AVFramePtr FFmpegDecoder::prepareVideoFrameForQueue"):
                               decoder_cpp.find("AVFramePtr FFmpegDecoder::normalizeVideoFrame")]
    require("transferHardwareFrameToCpu" in prepare_body and
            prepare_body.find("transferHardwareFrameToCpu") < prepare_body.find("normalizeVideoFrame"),
            "hardware frames should be transferred before normalizeVideoFrame")
    require("m_hardwareTransferFailureCount" in decoder_h,
            "FFmpegDecoder should count hardware transfer failures")
    require("MaxHardwareTransferFailureLogs" in decoder_cpp,
            "hardware transfer failure logs should be throttled")
    require("hardware frame transfer failed" in decoder_cpp,
            "decoder should log hardware transfer failures at the decode boundary")
    require("LOG_WARN(\"VideoToolboxBackend: av_hwframe_transfer_data failed" not in videotoolbox_cpp,
            "backend should not emit one warning for every failed transfer")


if __name__ == "__main__":
    main()
