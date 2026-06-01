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
    decoder_h = read("plugins/PlayPlugin/src/FFmpegDecoder.h")
    decoder_cpp = read("plugins/PlayPlugin/src/FFmpegDecoder.cpp")
    surface_cpp = read("plugins/PlayPlugin/src/FFmpegSurface.cpp")
    renderer_cpp = read("plugins/PlayPlugin/src/VideoRenderer.cpp")
    renderer_h = read("plugins/PlayPlugin/src/VideoRenderer.h")
    control_qml = read("plugins/PlayPlugin/qml/ControlBar.qml")
    playlist_qml = read("plugins/PlayPlugin/qml/PlaylistView.qml")
    playplugin_qml = read("plugins/PlayPlugin/qml/PlayPluginView.qml")
    player_qml = read("plugins/PlayPlugin/qml/PlayerView.qml")

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
    require("setAudioClockEnabled" in renderer_h and "m_audioClockEnabled" in renderer_h,
            "VideoRenderer should explicitly support video-only clocking")
    require("m_videoClock.restart()" in renderer_cpp and "m_videoClockBaseUs" in renderer_cpp,
            "video-only playback should establish a local clock from the first rendered frame")
    require("m_videoRenderer->setAudioClockEnabled(m_hasAudio)" in engine_cpp,
            "PlayerEngine should configure VideoRenderer clock mode from detected streams")

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
    require("normalizeVideoFrame" in decoder_h and "sws_getCachedContext" in decoder_cpp,
            "unsupported video pixel formats should be converted before rendering")


if __name__ == "__main__":
    main()
