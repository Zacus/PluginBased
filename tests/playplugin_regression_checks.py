#!/usr/bin/env python3

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def file_exists(path):
    return (ROOT / path).exists()


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def require_absent(needle, files, message):
    hits = [path for path, content in files if needle in content]
    require(not hits, f"{message}: {needle} found in {', '.join(hits)}")


def main():
    engine_h = read("plugins/PlayPlugin/src/PlayerEngine.h")
    engine_cpp = read("plugins/PlayPlugin/src/PlayerEngine.cpp")
    pipeline_cpp = read("plugins/PlayPlugin/src/PlaybackPipeline.cpp")
    seek_coordinator_cpp = read("plugins/PlayPlugin/src/PlaybackSeekCoordinator.cpp")
    ffmpeg_utils_h = read("plugins/PlayPlugin/src/FFmpegUtils.h")
    decoder_h = read("plugins/PlayPlugin/src/FFmpegDecoder.h")
    decoder_cpp = read("plugins/PlayPlugin/src/FFmpegDecoder.cpp")
    decode_perf_h = read("plugins/PlayPlugin/src/decode/DecodePerformance.h")
    decode_perf_cpp = read("plugins/PlayPlugin/src/decode/DecodePerformance.cpp")
    media_opener_h = read("plugins/PlayPlugin/src/decode/MediaOpener.h")
    media_opener_cpp = read("plugins/PlayPlugin/src/decode/MediaOpener.cpp")
    video_processor_h = read("plugins/PlayPlugin/src/decode/VideoFrameProcessor.h")
    video_processor_cpp = read("plugins/PlayPlugin/src/decode/VideoFrameProcessor.cpp")
    hw_backend_h = read("plugins/PlayPlugin/src/hw/HardwareDecoderBackend.h")
    hw_factory_h = read("plugins/PlayPlugin/src/hw/HardwareDecoderFactory.h")
    hw_factory_cpp = read("plugins/PlayPlugin/src/hw/HardwareDecoderFactory.cpp")
    videotoolbox_h = read("plugins/PlayPlugin/src/hw/VideoToolboxBackend.h")
    videotoolbox_cpp = read("plugins/PlayPlugin/src/hw/VideoToolboxBackend.cpp")
    native_frame_h = read("plugins/PlayPlugin/src/native/NativeVideoFrame.h")
    apple_bridge_h = read("plugins/PlayPlugin/src/native/AppleMetalVideoTextureBridge.h")
    apple_bridge_mm = read("plugins/PlayPlugin/src/native/AppleMetalVideoTextureBridge.mm")
    d3d11va_cpp = read("plugins/PlayPlugin/src/hw/D3D11VABackend.cpp")
    vaapi_cpp = read("plugins/PlayPlugin/src/hw/VaapiBackend.cpp")
    cmake = read("plugins/PlayPlugin/CMakeLists.txt")
    audio_cpp = read("plugins/PlayPlugin/src/AudioRenderer.cpp")
    audio_h = read("plugins/PlayPlugin/src/AudioRenderer.h")
    surface_cpp = read("plugins/PlayPlugin/src/FFmpegSurface.cpp")
    video_material_cpp = read("plugins/PlayPlugin/src/render/VideoMaterial.cpp")
    video_node_cpp = read("plugins/PlayPlugin/src/render/VideoNode.cpp")
    video_pixel_format_cpp = read("plugins/PlayPlugin/src/render/VideoPixelFormat.cpp")
    shader_frag = read("plugins/PlayPlugin/shaders/yuvvideo.frag")
    renderer_cpp = read("plugins/PlayPlugin/src/VideoRenderer.cpp")
    renderer_h = read("plugins/PlayPlugin/src/VideoRenderer.h")
    control_qml = read("plugins/PlayPlugin/qml/ControlBar.qml")
    playlist_qml = read("plugins/PlayPlugin/qml/PlaylistView.qml")
    playplugin_qml = read("plugins/PlayPlugin/qml/PlayPluginView.qml")
    player_qml = read("plugins/PlayPlugin/qml/PlayerView.qml")
    playplugin_cpp = read("plugins/PlayPlugin/PlayPlugin.cpp")
    context_h = read("plugins/PlayPlugin/src/PlaybackContext.h")
    app_plugin_h = read("plugin/IAppPlugin.h")
    plugin_cmake = read("plugin/CMakeLists.txt")
    dummy_cmake = read("plugins/DummyPlugin/CMakeLists.txt")
    manager_h = read("core/PluginManager.h")
    manager_cpp = read("core/PluginManager.cpp")
    playplugin_h = read("plugins/PlayPlugin/PlayPlugin.h")
    dummy_h = read("plugins/DummyPlugin/DummyPlugin.h")
    dummy_cpp = read("plugins/DummyPlugin/DummyPlugin.cpp")
    dummy_json = read("plugins/DummyPlugin/DummyPlugin.json")
    playplugin_json = read("plugins/PlayPlugin/PlayPlugin.json")
    readme = read("README.md")
    build_md = read("BUILD.md")
    root_cmake = read("CMakeLists.txt")
    app_cmake = read("app/CMakeLists.txt")
    core_cmake = read("core/CMakeLists.txt")
    logger_cmake = read("logger/CMakeLists.txt")
    main_cpp = read("app/main.cpp")
    app_controller_h = read("app/AppController.h")
    logger_cpp = read("logger/Logger.cpp")
    app_qml = read("app/qml/main.qml")
    home_panel_qml = read("app/qml/HomePanel.qml")
    package_yml = read("tools/package.yml")
    package_sh = read("package.sh")
    deploy_py = read("tools/deploy.py")

    require("project(PluginBased VERSION" in root_cmake,
            "top-level CMake project should be renamed to PluginBased")
    require("qt_add_executable(PluginBasedApp" in app_cmake,
            "host executable target should be PluginBasedApp")
    require('URI     "PluginBased"' in app_cmake,
            "host QML URI should be PluginBased")
    require('${CMAKE_BINARY_DIR}/PluginBased' in app_cmake,
            "host QML output directory should be PluginBased")
    require("PluginBasedLogger" in app_cmake and "PluginBasedPlugin" in app_cmake and
            "PluginBasedCore" in app_cmake,
            "host app should link renamed internal targets")
    require("PluginBasedCore" in core_cmake and "PluginBasedLogger" in core_cmake and
            "PluginBasedPlugin" in core_cmake,
            "core CMake target and dependencies should use PluginBased names")
    require("qt_add_qml_module(PluginBasedLogger" in logger_cmake,
            "logger target should be PluginBasedLogger")
    require('app.setOrganizationName("PluginBased")' in main_cpp,
            "Qt organization name should be PluginBased")
    require('app.setApplicationName("PluginBased")' in main_cpp,
            "Qt application name should be PluginBased")
    require('cfg.load(dataDir + "/config/pluginbased.ini")' in main_cpp,
            "config filename should be pluginbased.ini")
    require('qrc:/PluginBased/qml/main.qml' in main_cpp,
            "QML entry URL should use PluginBased")
    require('LOG_INFO("=== PluginBased starting (v1.0.0) ===")' in main_cpp,
            "startup log should use PluginBased")
    require('LOG_INFO("=== PluginBased exiting ({}) ===", ret)' in main_cpp,
            "shutdown log should use PluginBased")
    require('QStringLiteral("PluginBased")' in app_controller_h,
            "AppController should expose PluginBased as app name")
    require('"/pluginbased.log"' in logger_cpp,
            "logger should write pluginbased.log")
    require("import PluginBased 1.0" in app_qml and "import PluginBased 1.0" in home_panel_qml,
            "host QML files should import PluginBased 1.0")
    require("name: PluginBased" in package_yml and "binary: PluginBasedApp" in package_yml and
            "bundle_id: com.pluginbased.app" in package_yml,
            "packaging metadata should use PluginBased")
    require("- PluginBased" in package_yml,
            "packaging QML modules should include PluginBased")
    require("PluginBasedApp" in package_sh and "project(PluginBased VERSION" in package_sh,
            "package.sh should discover the renamed project and app bundle")
    require('exec "${DIR}/bin/PluginBasedApp" "$@"' in deploy_py,
            "deployment wrapper should launch PluginBasedApp")

    # Historical docs are intentionally excluded from this negative rename gate.
    active_build_files = [
        ("CMakeLists.txt", root_cmake),
        ("app/CMakeLists.txt", app_cmake),
        ("core/CMakeLists.txt", core_cmake),
        ("logger/CMakeLists.txt", logger_cmake),
        ("plugin/CMakeLists.txt", plugin_cmake),
        ("plugins/PlayPlugin/CMakeLists.txt", cmake),
        ("plugins/DummyPlugin/CMakeLists.txt", dummy_cmake),
        ("package.sh", package_sh),
    ]
    active_runtime_files = [
        ("app/main.cpp", main_cpp),
        ("app/AppController.h", app_controller_h),
        ("logger/Logger.cpp", logger_cpp),
        ("app/qml/main.qml", app_qml),
        ("app/qml/HomePanel.qml", home_panel_qml),
        ("plugin/IAppPlugin.h", app_plugin_h),
        ("plugins/PlayPlugin/PlayPlugin.json", playplugin_json),
        ("plugins/DummyPlugin/DummyPlugin.json", dummy_json),
        ("tools/package.yml", package_yml),
        ("tools/deploy.py", deploy_py),
    ]

    for old_target in (
            "VideoPlayerApp",
            "VideoPlayerCore",
            "VideoPlayerLogger",
            "VideoPlayerPlugin"):
        require_absent(old_target, active_build_files + active_runtime_files,
                       "old VideoPlayer build target should be removed from active files")

    require_absent('URI     "VideoPlayer"', [("app/CMakeLists.txt", app_cmake)],
                   "old host QML URI should be removed from active app CMake")
    require_absent('${CMAKE_BINARY_DIR}/VideoPlayer', [("app/CMakeLists.txt", app_cmake)],
                   "old host QML output directory should be removed from active app CMake")
    require_absent('qrc:/VideoPlayer/qml/main.qml', [("app/main.cpp", main_cpp)],
                   "old QML entry URL should be removed from active startup code")
    require_absent("VideoPlayer starting", [("app/main.cpp", main_cpp)],
                   "old startup log text should be removed from active startup code")
    require_absent("VideoPlayer exiting", [("app/main.cpp", main_cpp)],
                   "old shutdown log text should be removed from active startup code")
    require_absent("import VideoPlayer 1.0",
                   [("app/qml/main.qml", app_qml),
                    ("app/qml/HomePanel.qml", home_panel_qml)],
                   "old host QML imports should be removed from active QML")
    require_absent("videoplayer.ini", [("app/main.cpp", main_cpp)],
                   "old config filename should be removed from active startup code")
    require_absent("videoplayer.log", [("logger/Logger.cpp", logger_cpp)],
                   "old log filename should be removed from active logger code")
    require_absent("com.videoplayer.IAppPlugin/1.0",
                   [("plugin/IAppPlugin.h", app_plugin_h),
                    ("plugins/PlayPlugin/PlayPlugin.json", playplugin_json),
                    ("plugins/DummyPlugin/DummyPlugin.json", dummy_json)],
                   "old app plugin IID should be removed from active plugin metadata")
    require_absent("name: VideoPlayer", [("tools/package.yml", package_yml)],
                   "old package name should be removed from active packaging metadata")
    require_absent("binary: VideoPlayerApp", [("tools/package.yml", package_yml)],
                   "old package binary should be removed from active packaging metadata")
    require_absent("bundle_id: com.myorg.videoplayer", [("tools/package.yml", package_yml)],
                   "old bundle id should be removed from active packaging metadata")
    require_absent("- VideoPlayer", [("tools/package.yml", package_yml)],
                   "old QML module entry should be removed from active packaging metadata")
    require_absent('exec "${DIR}/bin/VideoPlayerApp" "$@"', [("tools/deploy.py", deploy_py)],
                   "old deployment wrapper target should be removed from active deployment script")

    require("class IAppPlugin" in app_plugin_h,
            "generic app plugin interface should exist")
    require("#define IAppPlugin_IID" in app_plugin_h and
            "com.pluginbased.IAppPlugin/1.0" in app_plugin_h,
            "IAppPlugin should expose the generic PluginBased plugin IID")
    require("PluginContext" not in app_plugin_h,
            "IAppPlugin should not define an unused host context")
    require(not file_exists("plugin/IPlayerPlugin.h"),
            "IPlayerPlugin interface should be removed from the host plugin contract")
    require("IPlayerPlugin" not in app_plugin_h,
            "IAppPlugin should not mention player-specific interfaces")
    require("PlayerPluginFinder" not in app_plugin_h and "findPlayerPlugin" not in app_plugin_h,
            "IAppPlugin should not expose player-specific callbacks")
    require("virtual QString id()          const = 0" in app_plugin_h,
            "IAppPlugin should expose a stable plugin id")
    require("virtual bool initialize() = 0" in app_plugin_h,
            "IAppPlugin initialize should not require a context argument")
    require("Q_DECLARE_INTERFACE(IAppPlugin, IAppPlugin_IID)" in app_plugin_h,
            "IAppPlugin should be declared as a Qt plugin interface")
    require("PluginBasedPlugin" in plugin_cmake,
            "PluginBasedPlugin interface target should publish IAppPlugin.h")
    require("IPlayerPlugin" not in plugin_cmake,
            "PluginBasedPlugin target should not publish IPlayerPlugin.h")
    require('#include "IAppPlugin.h"' in manager_h,
            "PluginManager should include the generic app plugin interface")
    require("IAppPlugin*" in manager_h,
            "PluginManager should store generic app plugins")
    require("IPlayerPlugin" not in manager_h and "IPlayerPlugin" not in manager_cpp,
            "PluginManager should not mention player-specific interfaces")
    require("findPlayerPlugin" not in manager_h and "findPlayerPlugin" not in manager_cpp,
            "PluginManager should not expose player capability lookup")
    require("qobject_cast<IAppPlugin*>" in manager_cpp,
            "PluginManager should load generic app plugins")
    require("PluginContext" not in manager_cpp and "plugin->initialize()" in manager_cpp,
            "PluginManager should call initialize without a context object")
    require("#include \"IAppPlugin.h\"" in playplugin_h,
            "PlayPlugin should include IAppPlugin")
    require("IPlayerPlugin" not in playplugin_h and "IPlayerPlugin" not in playplugin_cpp,
            "PlayPlugin should not implement the removed host player interface")
    require("public IAppPlugin" in playplugin_h,
            "PlayPlugin should remain a generic app plugin")
    require("Q_INTERFACES(IAppPlugin)" in playplugin_h,
            "PlayPlugin should expose only the generic app plugin interface")
    require("Q_INTERFACES(IAppPlugin IPlayerPlugin)" not in playplugin_h,
            "PlayPlugin should not expose a player plugin Qt interface")
    require("Q_PLUGIN_METADATA(IID IAppPlugin_IID FILE \"PlayPlugin.json\")" in playplugin_h,
            "PlayPlugin metadata should use the generic app plugin IID")
    require("com.pluginbased.IAppPlugin/1.0" in playplugin_json,
            "PlayPlugin JSON metadata should use the generic PluginBased app plugin IID")
    require("QString id()          const override" in playplugin_h,
            "PlayPlugin should expose a stable app plugin id")
    require("bool initialize() override" in playplugin_h,
            "PlayPlugin should initialize without PluginContext")
    require("PlaybackContext::instance().setFinder" not in playplugin_cpp,
            "PlayPlugin should not receive host player lookup callbacks")
    require("PlayerPluginFinder" not in context_h and "IPlayerPlugin" not in context_h,
            "PlaybackContext should not store host-provided player capabilities")
    require("#include \"IAppPlugin.h\"" in dummy_h,
            "DummyPlugin should include IAppPlugin")
    require("public IAppPlugin" in dummy_h and "public IPlayerPlugin" not in dummy_h,
            "DummyPlugin should be a generic app plugin only")
    require("Q_INTERFACES(IAppPlugin)" in dummy_h,
            "DummyPlugin should expose only the generic app plugin interface")
    require("Q_PLUGIN_METADATA(IID IAppPlugin_IID FILE \"DummyPlugin.json\")" in dummy_h,
            "DummyPlugin metadata should use generic app plugin IID")
    require("com.pluginbased.IAppPlugin/1.0" in dummy_json,
            "DummyPlugin JSON metadata should use generic PluginBased app plugin IID")
    require("bool initialize() override" in dummy_h,
            "DummyPlugin should initialize without PluginContext")
    require("canHandle(" not in dummy_h and "open(const QUrl&" not in dummy_h,
            "DummyPlugin should not expose playback capability methods")
    require("DummyPlugin::canHandle" not in dummy_cpp and "DummyPlugin::open" not in dummy_cpp,
            "DummyPlugin implementation should not contain playback methods")
    require("IAppPlugin" in readme and "IPlayerPlugin" not in readme,
            "README should describe only IAppPlugin as the plugin contract")
    require("通用插件" in readme,
            "README should describe the host plugin model as generic")
    require("IAppPlugin" in build_md and "IPlayerPlugin" not in build_md,
            "BUILD.md should describe only IAppPlugin as the plugin contract")

    require("finishMedia()" in engine_h, "PlayerEngine should centralize media completion")
    require("maybeFinishMedia()" in engine_h, "PlayerEngine should wait for active streams to drain")
    require("endOfAudio" in read("plugins/PlayPlugin/src/AudioRenderer.h"),
            "AudioRenderer should report audio queue EOF")
    require("PlaybackCompletionTracker m_completion" in engine_h,
            "PlayerEngine should track completed media through PlaybackCompletionTracker")
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

    require("bytesPerSample" in video_material_cpp,
            "black placeholder upload should account for R16 byte width")
    require('LOG_DEBUG("VideoRenderer: pts=' not in renderer_cpp,
            "per-frame video sync debug logging should be removed or throttled")
    require('LOG_DEBUG("VideoRenderer: drop frame' not in renderer_cpp,
            "per-frame drop logging should stay out of the render loop")
    require('LOG_DEBUG("FFmpegDecoder: converted video pixel format' not in decoder_cpp,
            "per-frame pixel format conversion logging should stay out of the decode loop")
    require("DecodePerformanceStats" in decode_perf_h and
            "DecodePerformanceLogger" in decoder_h and
            "PlayPerf: decoder" in decode_perf_cpp,
            "decoder should report throttled playback performance summaries")
    require("QElapsedTimer m_logTimer" in decode_perf_h and
            "PerformanceLogIntervalMs" in decode_perf_cpp,
            "decode performance logs should be time-throttled")
    require("VideoRenderPerformanceStats" in renderer_h and
            "maybeLogRenderPerformance" in renderer_cpp and
            "PlayPerf: renderer" in renderer_cpp,
            "renderer should report throttled playback performance summaries")
    require("m_renderPerfLogTimer" in renderer_h and
            "PerformanceLogIntervalMs" in renderer_cpp,
            "render performance logs should be time-throttled")
    require('LOG_DEBUG("PlayPerf: decoder frame' not in decoder_cpp and
            'LOG_DEBUG("PlayPerf: renderer frame' not in renderer_cpp,
            "playback performance logging should not run per frame")
    require("setAudioClockEnabled" in renderer_h and "m_audioClockEnabled" in renderer_h,
            "VideoRenderer should explicitly support video-only clocking")
    require("m_videoClock.restart()" in renderer_cpp and "m_videoClockBaseUs" in renderer_cpp,
            "video-only playback should establish a local clock from the first rendered frame")
    require("m_videoRenderer->setAudioClockEnabled(hasAudio)" in pipeline_cpp,
            "PlaybackPipeline should configure VideoRenderer clock mode from detected streams")
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
    require("m_audioRenderer.setAcceptedSerial(serial)" in seek_coordinator_cpp and
            "m_videoRenderer.completeSeek(generation, serial)" in seek_coordinator_cpp,
            "PlaybackSeekCoordinator should apply seek serial to all frame consumers")
    require("entry.serial != m_acceptedSerial" in read("plugins/PlayPlugin/src/AudioRenderer.cpp") and
            "entry.serial != m_acceptedSerial" in renderer_cpp,
            "frame consumers should discard stale frames from older seek serials")
    require("quint64 channelLayoutMask" in decoder_h and
            "m_audioChannelLayoutMask" in decoder_h,
            "decoder should expose the source audio channel layout mask")
    require("actx->ch_layout.u.mask" in media_opener_cpp,
            "decoder should capture FFmpeg's native channel layout mask")
    require("m_srcChannelLayoutMask" in audio_h and
            "av_channel_layout_from_mask" in audio_cpp and
            "av_channel_layout_default(&srcLayout, m_srcChannels)" in audio_cpp,
            "AudioRenderer should initialize swresample from the real or default source layout")
    require("setPendingOpenUrl" not in context_h and "takePendingOpenUrl" not in context_h,
            "PlaybackContext should not store host-originated pending open requests")
    require("PlaybackContext::instance().setPendingOpenUrl" not in playplugin_cpp,
            "PlayPlugin should not cache host open requests")
    require("takePendingOpenUrl" not in engine_cpp,
            "PlayerEngine should not consume host-originated pending open requests")
    require("buildColorMatrix" not in surface_cpp and "colorMatrix" not in surface_cpp,
            "FFmpegSurface should not keep obsolete color matrix code or comments")
    require("AV_PIX_FMT_NV12" in video_processor_cpp and "AV_PIX_FMT_P010LE" in video_processor_cpp,
            "decoder should let NV12 and P010 frames bypass sws normalization")
    require("PlaneLayout" in video_pixel_format_cpp and "Semiplanar" in video_pixel_format_cpp,
            "VideoPixelFormat should distinguish planar and semiplanar YUV layouts")
    require("QRhiTexture::RG8" in video_pixel_format_cpp and "QRhiTexture::RG16" in video_pixel_format_cpp,
            "NV12/P010 UV planes should upload as two-channel RHI textures")
    require("formatMode" in video_pixel_format_cpp and "formatMode" in video_material_cpp,
            "VideoPixelFormat should pass a compact shader format mode through VideoMaterial")
    require("needs10BitExpansion" in video_pixel_format_cpp and "needs10bitExpansion" in shader_frag,
            "P010 should not reuse planar low-10bit expansion and cause color or brightness shifts")
    require(".rg" in shader_frag and "semiplanar" in shader_frag,
            "shader should sample NV12/P010 UV from texU.rg")
    require("NativeVideoFrame" in native_frame_h and "NativeFrameKind" in native_frame_h,
            "native video frame metadata should exist for hardware-backed frames")
    require("CVMetalTextureCacheCreateTextureFromImage" in apple_bridge_mm,
            "Apple bridge should create Metal textures from CVPixelBuffer planes")
    require("QRhiTexture::NativeTexture" in apple_bridge_mm and "createFrom" in apple_bridge_mm,
            "Apple bridge should wrap Metal textures with QRhiTexture::createFrom")
    require("AV_PIX_FMT_VIDEOTOOLBOX" in video_processor_cpp and
            "shouldPreserveHardwareFrameForDirectRender" in video_processor_cpp,
            "decoder should preserve VideoToolbox frames when native render is enabled")
    require("transferHardwareFrameToCpu" in video_processor_cpp and "nativeFallbackVideoFrames" in decode_perf_h,
            "native render failures should be observable and keep CPU fallback available")
    require("AppleMetalVideoTextureBridge" in video_node_cpp and "setNativeFrame" in video_node_cpp,
            "VideoNode should consume native VideoToolbox frames")
    require("supportsNativeVideoToolboxRendering" in surface_cpp and
            "setVideoToolboxDirectRenderingEnabled" in decoder_h and
            "nativeRenderingFailed" in surface_cpp,
            "native rendering should be enabled by a Surface-to-Decoder capability handshake")
    require("CoreVideo" in cmake and "Metal" in cmake and "QuartzCore" in cmake,
            "PlayPlugin should link Apple frameworks for CVMetalTextureCache")
    ensure_textures = video_node_cpp[video_node_cpp.find("void VideoNode::ensureTextures"):
                                     video_node_cpp.find("void VideoNode::releaseTextures")]
    require(ensure_textures.count("m_material.paramsDirty") == 1 and
            ensure_textures.count("m_material.cachedFullRange") == 1 and
            ensure_textures.count("m_material.cachedBt709") == 1,
            "texture recreation should mark material state dirty once")

    require("import QuickUI.Components 1.0" in control_qml and
            "import QuickUI.Components 1.0" in playlist_qml and
            "import QuickUI.Components 1.0" in playplugin_qml and
            "import QuickUI.Components 1.0" in player_qml,
            "QML dependency on QuickUI should be explicit")
    require("ComponentTheme.surface" in playplugin_qml and
            "ComponentTheme.separator" in playplugin_qml,
            "PlayPlugin shell should follow ComponentTheme surface tokens")
    require("ComponentTheme.textPrimary" in player_qml and
            "ComponentTheme.textSecondary" in player_qml,
            "PlayerView placeholders should follow ComponentTheme text tokens")
    require("ComponentTheme.trackBg" in control_qml and
            "ComponentTheme.accent" in control_qml,
            "ControlBar sliders should follow ComponentTheme track and accent tokens")
    require("ComponentTheme.surface" in playlist_qml and
            "ComponentTheme.textPrimary" in playlist_qml and
            "ComponentTheme.accent" in playlist_qml,
            "PlaylistView should follow ComponentTheme tokens")
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
    require("normalizeVideoFrame" in video_processor_h and "sws_getCachedContext" in video_processor_cpp,
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
    require('#include "hw/HardwareDecoderFactory.h"' in media_opener_cpp,
            "MediaOpener should include the hardware backend factory")
    require("std::unique_ptr<HardwareDecoderBackend> m_hardwareDecoder" in decoder_h,
            "FFmpegDecoder should own the selected hardware backend")
    require("createHardwareDecoderBackend(codec, stream->codecpar->codec_id)" in media_opener_cpp,
            "MediaOpener should ask the factory for video hardware decoding")
    require("m_hardwareDecoder.reset();" in decoder_cpp[decoder_cpp.find("void FFmpegDecoder::closeInternal"):],
            "FFmpegDecoder should release hardware backend on close")
    require("Q_OS_APPLE" not in decoder_cpp and
            "Q_OS_WIN" not in decoder_cpp and
            "Q_OS_LINUX" not in decoder_cpp,
            "FFmpegDecoder should not contain platform branching for hardware backend selection")
    require("bool openVideoCodec(OpenedMedia& media, AVStream* stream, const AVCodec* codec)" in media_opener_h,
            "MediaOpener should open video codec through a retryable helper")
    require("m_mediaOpener.open(path)" in decoder_cpp,
            "openInternal should delegate media opening")
    require("configureContext(vctx)" in media_opener_cpp,
            "video codec helper should configure hardware before avcodec_open2")
    require("fallback to software decoding" in media_opener_cpp,
            "hardware open failure should log software fallback")
    require("AVCodecContext* MediaOpener::createVideoCodecContext" in media_opener_cpp and
            "vctx = createVideoCodecContext(stream, codec)" in media_opener_cpp,
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
    require("copyFrameMetadata" in video_processor_cpp,
            "FFmpegDecoder should preserve timing and color metadata after hardware transfer")
    require("prepareForQueue" in video_processor_h and
            "m_videoFrameProcessor.prepareForQueue" in decoder_cpp,
            "FFmpegDecoder should prepare video frames through VideoFrameProcessor before queueing")
    prepare_body = video_processor_cpp[video_processor_cpp.find("AVFramePtr VideoFrameProcessor::prepareForQueue"):
                                       video_processor_cpp.find("bool VideoFrameProcessor::shouldPreserveHardwareFrameForDirectRender")]
    require("transferHardwareFrameToCpu" in prepare_body and
            prepare_body.find("transferHardwareFrameToCpu") < prepare_body.find("normalizeVideoFrame"),
            "hardware frames should be transferred before normalizeVideoFrame")
    require("m_hardwareTransferFailureCount" in video_processor_h,
            "VideoFrameProcessor should count hardware transfer failures")
    require("MaxHardwareTransferFailureLogs" in video_processor_cpp,
            "hardware transfer failure logs should be throttled")
    require("hardware frame transfer failed" in video_processor_cpp,
            "decoder should log hardware transfer failures at the decode boundary")
    require("LOG_WARN(\"VideoToolboxBackend: av_hwframe_transfer_data failed" not in videotoolbox_cpp,
            "backend should not emit one warning for every failed transfer")


if __name__ == "__main__":
    main()
