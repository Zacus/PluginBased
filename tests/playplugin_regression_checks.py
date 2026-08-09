#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def player_engine_method_body(source: str, name: str) -> str:
    start = source.index(f"void PlayerEngine::{name}(")
    end = source.find("\nvoid PlayerEngine::", start + 1)
    return source[start:] if end == -1 else source[start:end]


def require_before(body: str, earlier: str, later: str, message: str) -> None:
    require(earlier in body and later in body, message)
    require(body.index(earlier) < body.index(later), message)


def main() -> None:
    engine_h = read("plugins/PlayPlugin/src/playback/PlayerEngine.h")
    engine_cpp = read("plugins/PlayPlugin/src/playback/PlayerEngine.cpp")
    playback_seek_cpp = read("plugins/PlayPlugin/src/playback/PlaybackSeek.cpp")
    pipeline_h = read("plugins/PlayPlugin/src/playback/PlaybackPipeline.h")
    pipeline_cpp = read("plugins/PlayPlugin/src/playback/PlaybackPipeline.cpp")
    sdk_adapter_h = read("plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.h")
    sdk_adapter_cpp = read("plugins/PlayPlugin/src/playback/SdkPlaybackAdapter.cpp")
    presenter_cpp = read("plugins/PlayPlugin/src/playback/QtRhiVideoPresenter.cpp")
    ffmpeg_surface_cpp = read("plugins/PlayPlugin/src/video/FFmpegSurface.cpp")
    control_bar_qml = read("plugins/PlayPlugin/qml/ControlBar.qml")
    playplugin_translation = read("plugins/PlayPlugin/translations/PlayPlugin_zh_CN.ts")
    cmake = read("plugins/PlayPlugin/CMakeLists.txt")

    require("PlaybackCompletionTracker" in engine_h,
            "PlayerEngine should keep completion tracking for SDK EOF/audio/video drain")
    require("m_completion.setStreams" in engine_cpp,
            "PlayerEngine should derive completion streams from SDK media info")
    require("m_pipeline->seek(targetPositionMs, seekGeneration, resumeAfterSeek)" in engine_cpp,
            "PlayerEngine should keep generation-based seek completion and pass resume intent")
    require("Q_INVOKABLE void seekBy(qint64 deltaMs)" in engine_h
            and "PlaybackSeekState m_seekState" in engine_h
            and "calculateRelativeSeekTarget" in engine_cpp
            and "m_seekState.basePosition(m_position)" in engine_cpp,
            "PlayerEngine should calculate relative seek targets and track pending state outside QML")
    require("!m_seekState.acceptsPositionUpdate()" in engine_cpp
            and "m_seekState.complete(generation)" in engine_cpp,
            "PlayerEngine should reject stale positions until the latest seek completes")
    require("m_position = posMs" in engine_cpp[engine_cpp.find("void PlayerEngine::onDecoderPosition"):],
            "SDK decoder position should drive UI progress")
    require("Q_PROPERTY(bool       canSeekForward" in engine_h
            and "canSeekForwardChanged" in engine_h
            and "refreshCanSeekForward" in engine_cpp
            and "deltaMs > 0 && !m_canSeekForward" in engine_cpp,
            "PlayerEngine should expose authoritative forward-seek availability")

    require("bool updateCanSeekForward()" in engine_h
            and "bool PlayerEngine::updateCanSeekForward()" in engine_cpp,
            "PlayerEngine should separate forward-seek cache updates from notifications")

    refresh = "refreshCanSeekForward();"
    cache_update = "updateCanSeekForward();"
    can_notify = "emit canSeekForwardChanged(m_canSeekForward);"
    require(cache_update in player_engine_method_body(engine_cpp, "refreshCanSeekForward")
            and can_notify in player_engine_method_body(engine_cpp, "refreshCanSeekForward"),
            "refreshCanSeekForward should notify only after updating the cache")

    seek_body = player_engine_method_body(engine_cpp, "seek")
    require_before(seek_body, "m_seekState.begin(targetPositionMs)", "m_pipeline->seek(",
                   "seek should dispatch after starting its generation")
    invalid_generation_index = seek_body.find("if (seekGeneration == 0)")
    dispatch_index = seek_body.find("m_pipeline->seek(targetPositionMs, seekGeneration, resumeAfterSeek)")
    pending_guard_index = seek_body.find("!m_seekState.isPending(seekGeneration)")
    optimistic_update_index = seek_body.find("m_position = targetPositionMs")
    require(-1 not in (invalid_generation_index, dispatch_index)
            and invalid_generation_index < dispatch_index,
            "PlayerEngine should reject exhausted seek generations before dispatch")
    require(-1 not in (dispatch_index, pending_guard_index, optimistic_update_index)
            and dispatch_index < pending_guard_index < optimistic_update_index,
            "PlayerEngine should ignore optimistic updates invalidated by synchronous seek errors")
    require_before(seek_body, "m_pipeline->seek(", "m_position = targetPositionMs;",
                   "seek should dispatch before optimistic position updates")
    require_before(seek_body, "m_position = targetPositionMs;", refresh,
                   "seek should refresh after optimistic position updates")
    require_before(seek_body, refresh, "emit positionChanged(m_position);",
                   "seek should refresh before positionChanged")
    reset_version_capture_index = seek_body.find(
        "const quint64 seekResetVersion = m_seekState.resetVersion();")
    position_notification_index = seek_body.find("emit positionChanged(m_position);")
    resume_guard_index = seek_body.find(
        "if (resumeAfterSeek && m_seekState.resetVersion() == seekResetVersion)")
    playing_state_index = seek_body.find("setState(Playing);")
    require(-1 not in (reset_version_capture_index, dispatch_index,
                       position_notification_index, resume_guard_index, playing_state_index)
            and reset_version_capture_index < dispatch_index
            and position_notification_index < resume_guard_index < playing_state_index,
            "PlayerEngine should revalidate its reset version after UI notifications before resuming")
    require("if (resumeAfterSeek && m_seekState.isPending(seekGeneration))" not in seek_body,
            "PlayerEngine should not treat normal seek supersession as a reset before resuming")

    begin_body = playback_seek_cpp[
        playback_seek_cpp.find("int PlaybackSeekState::begin(qint64 targetPositionMs)"):
        playback_seek_cpp.find("qint64 PlaybackSeekState::basePosition")]
    reset_body = playback_seek_cpp[
        playback_seek_cpp.find("void PlaybackSeekState::reset()"):
    ]
    require("m_generation == std::numeric_limits<int>::max()" in begin_body
            and "return 0;" in begin_body
            and "m_generation = 0;" not in begin_body
            and "m_generation =" not in reset_body,
            "PlaybackSeekState should preserve issued generations and reject exhaustion")

    for method in ("onDecoderPosition", "onAudioPosition", "onVideoPosition"):
        body = player_engine_method_body(engine_cpp, method)
        require_before(body, "m_position = posMs;", refresh,
                       f"{method} should refresh after writing position")
        require_before(body, refresh, "emit positionChanged(m_position);",
                       f"{method} should refresh before positionChanged")

    for method, commits, notifications in (
        ("finishMedia", ("m_pipeline->setPaused(true);", "setState(Paused);"),
         (can_notify, "emit endOfMedia();")),
        ("stop", ("stopAllComponents();", "m_pipeline->clearSurface();", "setState(Stopped);",
                  "delete previousMediaInfo;"),
         (can_notify, "emit positionChanged(m_position);", "emit durationChanged(m_duration);",
          "emit currentMediaChanged(nullptr);")),
        ("onDecoderError", ("stopAllComponents();", "setState(Stopped);"),
         (can_notify, "setError(msg);", "emit positionChanged(m_position);", "emit durationChanged(m_duration);")),
        ("onMediaInfoReady", ("m_pipeline->startRenderersForMedia(", "delete previousMediaInfo;"),
         (can_notify, "emit durationChanged(m_duration);", "emit currentMediaChanged(m_mediaInfo);")),
    ):
        body = player_engine_method_body(engine_cpp, method)
        require(refresh not in body,
                f"{method} should defer canSeekForwardChanged until its transition commits")
        for commit in commits:
            require_before(body, cache_update, commit,
                           f"{method} should update the cache before {commit}")
        for earlier, later in zip(commits, commits[1:]):
            require_before(body, earlier, later,
                           f"{method} should commit {earlier} before {later}")
        for notification in notifications:
            require_before(body, commits[-1], notification,
                           f"{method} should complete commits before {notification}")

    require("std::unique_ptr<SdkPlaybackAdapter>" in pipeline_h,
            "PlaybackPipeline should use SDK playback adapter")
    require("FfmpegAudioTempoProcessor" in pipeline_h + pipeline_cpp,
            "PlaybackPipeline should own the FFmpeg tempo processor")
    require("CoreAudioAudioOutput" in pipeline_cpp,
            "PlaybackPipeline should inject SDK platform audio output")
    require("QtRhiVideoPresenter" in pipeline_cpp,
            "PlaybackPipeline should inject Qt RHI video presenter")
    require("destroySdkRuntimeChain" in pipeline_cpp,
            "PlaybackPipeline should explicitly tear down SDK runtime chain")

    require("std::unique_ptr<ISdkPlaybackSession>" in sdk_adapter_h
            and "RealSdkPlaybackSession" in sdk_adapter_cpp,
            "SdkPlaybackAdapter should own an injectable facade over SDK PlaybackSession")
    require("acceptsEventSerial" in sdk_adapter_cpp,
            "SdkPlaybackAdapter should reject stale queued Qt callbacks")
    require("PlayPerf: sdk" in sdk_adapter_cpp,
            "SDK diagnostics should remain observable in PlayPlugin logs")
    require("PlaybackRateChangedEvent" in sdk_adapter_cpp
            and "m_rateChangePending" in sdk_adapter_h,
            "SdkPlaybackAdapter should confirm or roll back asynchronous rate changes")

    require("Q_PROPERTY(double     playbackRate" in engine_h
            and "playbackRateChangePending" in engine_h
            and "onPlaybackRateChanged" in engine_cpp,
            "PlayerEngine should expose confirmed playback rate and pending state")

    for token in (
        "QtPlaybackAdapter",
        "PlaybackDataBridge",
        "AudioRenderer",
        "VideoRenderer",
        "VideoFrameScheduler",
        "ClockSync",
        "FrameQueue",
        "LegacyQt",
    ):
        require(token not in pipeline_h + pipeline_cpp + cmake,
                f"PlayPlugin runtime path should not reference removed component: {token}")

    require("supportsNativeVideoToolboxRendering" in ffmpeg_surface_cpp + read("plugins/PlayPlugin/src/video/FFmpegSurface.h"),
            "FFmpegSurface should still expose native rendering capability")
    require("diagnosticsSnapshot()" in ffmpeg_surface_cpp,
            "FFmpegSurface should keep render diagnostics at the Qt rendering boundary")
    require("nativeRenderingFailed" in pipeline_cpp,
            "PlaybackPipeline should react to native rendering failures from the Qt surface")

    require("onMoved: root.seekRequested" not in control_bar_qml,
            "Seek bar should not flood SDK seek while dragging")
    require("onPressedChanged:" in control_bar_qml
            and "root.seekRequested(value * root.duration)" in control_bar_qml,
            "Seek bar should submit one seek when the drag is released")
    require("forwardRequested" in control_bar_qml
            and 'qsTr("Forward 3 seconds")' in control_bar_qml,
            "ControlBar should expose the three-second forward command")
    require("onForwardRequested:   engine.seekBy(3000)" in read("plugins/PlayPlugin/qml/PlayerView.qml"),
            "PlayerView should map the forward command to a three-second relative seek")
    require("property bool canSeekForward: false" in control_bar_qml
            and "readonly property bool canSeekForward" not in control_bar_qml
            and "canSeekForward: engine.canSeekForward"
                in read("plugins/PlayPlugin/qml/PlayerView.qml"),
            "ControlBar should consume PlayerEngine forward-seek availability")
    require("<source>Forward 3 seconds</source>" in playplugin_translation
            and "<translation>快进 3 秒</translation>" in playplugin_translation,
            "PlayPlugin should translate the three-second forward tooltip")
    require("ComboBox" in control_bar_qml
            and "playbackRateRequested" in control_bar_qml
            and all(rate in control_bar_qml
                    for rate in ("0.5x", "0.75x", "1.0x", "1.25x", "1.5x", "2.0x")),
            "ControlBar should expose all supported playback-rate presets")
    require("Layout.minimumWidth: 76" in control_bar_qml
            and "leftPadding: 0" in control_bar_qml
            and "rightPadding: 0" in control_bar_qml,
            "Playback-rate selector should preserve enough width for its complete label")
    require("popupMinimumWidth: 112" in control_bar_qml
            and "popup.width:" not in control_bar_qml,
            "Playback-rate selector should configure the public popup width API")
    require("playbackRate:  engine.playbackRate" in read("plugins/PlayPlugin/qml/PlayerView.qml")
            and "engine.playbackRate = rate" in read("plugins/PlayPlugin/qml/PlayerView.qml"),
            "PlayerView should bind the confirmed rate and submit rate selections")

    require("media_sdk::playback_session" in cmake,
            "PlayPlugin should link playback session")
    require("media_sdk::audio_ffmpeg" in cmake,
            "PlayPlugin should link the FFmpeg tempo processor")
    require("media_sdk::platform_audio_macos" in cmake,
            "PlayPlugin should link macOS SDK audio platform implementation on Apple")


if __name__ == "__main__":
    main()
