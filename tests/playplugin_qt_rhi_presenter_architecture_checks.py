from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLAYPLUGIN = ROOT / "plugins" / "PlayPlugin"
RUNTIME = ROOT / "sdk" / "media_playback_runtime"
PRESENTER_H = PLAYPLUGIN / "src" / "playback" / "QtRhiVideoPresenter.h"
PRESENTER_CPP = PLAYPLUGIN / "src" / "playback" / "QtRhiVideoPresenter.cpp"
SURFACE_H = PLAYPLUGIN / "src" / "video" / "FFmpegSurface.h"
SURFACE_CPP = PLAYPLUGIN / "src" / "video" / "FFmpegSurface.cpp"
VIDEO_NODE_CPP = PLAYPLUGIN / "src" / "video" / "render" / "VideoNode.cpp"
METAL_BRIDGE_MM = PLAYPLUGIN / "src" / "video" / "native" / "AppleMetalVideoTextureBridge.mm"
PLAYPLUGIN_CMAKE = PLAYPLUGIN / "CMakeLists.txt"
PLAYBACK_PIPELINE_H = PLAYPLUGIN / "src" / "playback" / "PlaybackPipeline.h"
PLAYBACK_PIPELINE_CPP = PLAYPLUGIN / "src" / "playback" / "PlaybackPipeline.cpp"
SDK_PLAYBACK_ADAPTER_H = PLAYPLUGIN / "src" / "playback" / "SdkPlaybackAdapter.h"
SDK_PLAYBACK_ADAPTER_CPP = PLAYPLUGIN / "src" / "playback" / "SdkPlaybackAdapter.cpp"
PLAYER_ENGINE_H = PLAYPLUGIN / "src" / "playback" / "PlayerEngine.h"
PLAYER_ENGINE_CPP = PLAYPLUGIN / "src" / "playback" / "PlayerEngine.cpp"
ROOT_CMAKE = ROOT / "CMakeLists.txt"

FORBIDDEN_RUNTIME_TOKENS = (
    "PlayPlugin",
    "FFmpegSurface",
    "QObject",
    "QQuickItem",
    "QSGNode",
    "QRhi",
    "QAudioSink",
    "#include <Q",
    "#include \"video/",
)

PLAYPLUGIN_SCENE_GRAPH_TOKENS = (
    "QRhi",
    "QSGNode",
    "QQuickWindow",
    "QSGRendererInterface",
)


def read(path: Path) -> str:
    if not path.exists():
        raise AssertionError(f"{path} must exist")
    return path.read_text(encoding="utf-8")


def assert_contains(text: str, token: str, source: Path) -> None:
    if token not in text:
        raise AssertionError(f"{source} must contain {token!r}")


def assert_not_contains(text: str, token: str, source: Path) -> None:
    if token in text:
        raise AssertionError(f"{source} must not contain {token!r}")


def assert_before(text: str, first: str, second: str, source: Path) -> None:
    first_index = text.find(first)
    second_index = text.find(second)
    if first_index < 0:
        raise AssertionError(f"{source} must contain {first!r}")
    if second_index < 0:
        raise AssertionError(f"{source} must contain {second!r}")
    if first_index > second_index:
        raise AssertionError(f"{source} must contain {first!r} before {second!r}")


def check_runtime_boundary() -> None:
    scanned = list(RUNTIME.rglob("*.h")) + list(RUNTIME.rglob("*.cpp"))
    if not scanned:
        raise AssertionError("sdk/media_playback_runtime must contain source/header files")

    for path in scanned:
        text = read(path)
        for token in FORBIDDEN_RUNTIME_TOKENS:
            assert_not_contains(text, token, path)


def check_presenter_contract() -> None:
    header = read(PRESENTER_H)
    source = read(PRESENTER_CPP)
    surface_header = read(SURFACE_H)
    surface_source = read(SURFACE_CPP)
    video_node_source = read(VIDEO_NODE_CPP)
    metal_bridge_source = read(METAL_BRIDGE_MM)
    combined = "\n".join((
        header,
        source,
        surface_header,
        surface_source,
        video_node_source,
        metal_bridge_source,
    ))

    assert_contains(header, "media_sdk/runtime/VideoPresenter.h", PRESENTER_H)
    assert_contains(header, "class QtRhiVideoPresenter final", PRESENTER_H)
    assert_contains(header, "media_sdk::runtime::IVideoPresenter", PRESENTER_H)
    assert_contains(header, "QPointer<FFmpegSurface>", PRESENTER_H)
    assert_contains(header, "std::atomic_uint64_t m_nextPresentId", PRESENTER_H)
    assert_contains(header, "setEvents(media_sdk::runtime::IVideoPresenterEvents* events)", PRESENTER_H)
    assert_contains(combined, "media_sdk::runtime::PresentCompletion", PRESENTER_CPP)
    assert_contains(combined, "UnsupportedNativeHandle", PRESENTER_CPP)
    assert_contains(combined, "QMetaObject::invokeMethod", PRESENTER_CPP)
    assert_contains(combined, "Qt::QueuedConnection", PRESENTER_CPP)
    assert_contains(combined, "frame.storage()", PRESENTER_CPP)
    assert_contains(combined, "av_frame_clone", PRESENTER_CPP)
    assert_contains(combined, "surface->onFrameReady", PRESENTER_CPP)
    assert_contains(combined, "surface->clear()", PRESENTER_CPP)
    assert_contains(source, "diagnosticsSnapshot()", PRESENTER_CPP)
    assert_contains(source, "media_sdk::runtime::PresentDiagnostics", PRESENTER_CPP)
    assert_contains(source, "afterRendering", PRESENTER_CPP)
    assert_contains(source, "Qt::SingleShotConnection", PRESENTER_CPP)
    assert_contains(source, "Qt::BlockingQueuedConnection", PRESENTER_CPP)
    assert_contains(source, "Qt video surface has no QQuickWindow", PRESENTER_CPP)
    assert_contains(source, "Qt scene graph did not draw the native VideoToolbox texture", PRESENTER_CPP)
    assert_contains(source, "diagnostics.nativeTextureFailed = 1", PRESENTER_CPP)

    for forbidden in (
        "sws_scale",
        "av_hwframe_transfer_data",
        "transferHardwareFrameToCpu",
    ):
        assert_not_contains(source, forbidden, PRESENTER_CPP)

    assert_contains(source, "PixelFormat::Native", PRESENTER_CPP)
    assert_contains(source, "NativeHandleKind::VideoToolboxPixelBuffer", PRESENTER_CPP)
    assert_contains(metal_bridge_source, "CVMetalTextureCacheCreateTextureFromImage", METAL_BRIDGE_MM)
    assert_contains(metal_bridge_source, "createFrom", METAL_BRIDGE_MM)
    assert_contains(metal_bridge_source, "releaseNativeTextures", METAL_BRIDGE_MM)

    for token in ("nativeTextureCreated", "nativeTextureDrawn", "cpuMemcpy", "cpuTransferred"):
        assert_contains(source, token, PRESENTER_CPP)


def check_scene_graph_dependency_location() -> None:
    allowed = {PRESENTER_H.resolve(), PRESENTER_CPP.resolve()}
    scanned = list((PLAYPLUGIN / "src" / "playback").rglob("*.h"))
    scanned += list((PLAYPLUGIN / "src" / "playback").rglob("*.cpp"))

    for path in scanned:
        if path.resolve() in allowed:
            continue
        text = read(path)
        for token in PLAYPLUGIN_SCENE_GRAPH_TOKENS:
            assert_not_contains(text, token, path)


def check_cmake_registration() -> None:
    playplugin_cmake = read(PLAYPLUGIN_CMAKE)
    root_cmake = read(ROOT_CMAKE)

    assert_contains(playplugin_cmake, "src/playback/QtRhiVideoPresenter.h", PLAYPLUGIN_CMAKE)
    assert_contains(playplugin_cmake, "src/playback/QtRhiVideoPresenter.cpp", PLAYPLUGIN_CMAKE)
    assert_contains(playplugin_cmake, "src/playback/SdkPlaybackAdapter.h", PLAYPLUGIN_CMAKE)
    assert_contains(playplugin_cmake, "src/playback/SdkPlaybackAdapter.cpp", PLAYPLUGIN_CMAKE)
    assert_contains(playplugin_cmake, "media_sdk::playback_runtime", PLAYPLUGIN_CMAKE)
    assert_contains(root_cmake, "playplugin_qt_rhi_presenter_architecture_checks", ROOT_CMAKE)


def check_sdk_playback_adapter_contract() -> None:
    header = read(SDK_PLAYBACK_ADAPTER_H)
    source = read(SDK_PLAYBACK_ADAPTER_CPP)
    pipeline_header = read(PLAYBACK_PIPELINE_H)
    pipeline_source = read(PLAYBACK_PIPELINE_CPP)
    combined = "\n".join((header, source))

    assert_contains(header, "class SdkPlaybackAdapter final", SDK_PLAYBACK_ADAPTER_H)
    assert_contains(header, "media_sdk::IEventSink", SDK_PLAYBACK_ADAPTER_H)
    assert_contains(header, "media_sdk::runtime::IRuntimePlayerEvents", SDK_PLAYBACK_ADAPTER_H)
    assert_contains(header, "onFallbackToCpuRequested", SDK_PLAYBACK_ADAPTER_H)
    assert_contains(header, "media_sdk::Player", SDK_PLAYBACK_ADAPTER_H)
    assert_contains(header, "media_sdk::runtime::RuntimePlayer", SDK_PLAYBACK_ADAPTER_H)

    for token in (
        "RuntimePlayerConfig",
        "runtimePlayer->open()",
        "runtimePlayer->enqueueAudio",
        "runtimePlayer->enqueueVideo",
        "runtimePlayer->enqueueEndOfStream",
        "runtimePlayer->completeSeek",
        "preferNativeVideoFrames = false",
        "RuntimeFallbackAction",
    ):
        assert_contains(combined, token, SDK_PLAYBACK_ADAPTER_CPP)

    assert_contains(pipeline_header, "SdkPlaybackAdapter", PLAYBACK_PIPELINE_H)
    assert_contains(pipeline_source, "m_sdkAdapter->openFile(url)", PLAYBACK_PIPELINE_CPP)
    assert_contains(pipeline_source, "m_sdkAdapter->setPaused(paused)", PLAYBACK_PIPELINE_CPP)
    assert_contains(pipeline_source, "m_sdkAdapter->seek(positionMs, generation)", PLAYBACK_PIPELINE_CPP)
    assert_contains(pipeline_source, "m_sdkAdapter->setVideoToolboxDirectRenderingEnabled(enabled)", PLAYBACK_PIPELINE_CPP)
    assert_contains(pipeline_source, "m_surface && m_surface->supportsNativeVideoToolboxRendering()", PLAYBACK_PIPELINE_CPP)
    assert_contains(pipeline_source, "&PlaybackPipeline::onNativeRenderingFailed", PLAYBACK_PIPELINE_CPP)
    assert_not_contains(pipeline_source, "decode event bridge is pending", PLAYBACK_PIPELINE_CPP)

    for token in (
        "struct PendingSeekRequest",
        "media_sdk::runtime::RuntimeTimeline runtimeTimeline",
        "acceptSeekCompletedEvent",
        "PendingSeekRequest { generation, runtimeTimeline }",
        "acceptedRuntimeTimelineForCoreEvent(event.metadata)",
        "acceptsRuntimeTimeline(timeline)",
    ):
        assert_contains(combined, token, SDK_PLAYBACK_ADAPTER_CPP)
    on_event_body = source[source.find("void SdkPlaybackAdapter::onEvent"):
                            source.find("void SdkPlaybackAdapter::onFallbackToCpuRequested")]
    assert_before(on_event_body,
                  "const auto seekCompletion = acceptSeekCompletedEvent(event);",
                  "if (handleDataEvent(event))",
                  SDK_PLAYBACK_ADAPTER_CPP)

    seek_completion_body = source[source.find("SdkPlaybackAdapter::acceptSeekCompletedEvent"):
                                  source.find("void SdkPlaybackAdapter::handleFallbackOnObjectThread")]
    assert_contains(seek_completion_body,
                    "return std::nullopt;",
                    SDK_PLAYBACK_ADAPTER_CPP)
    assert_before(seek_completion_body,
                  "return std::nullopt;",
                  "m_acceptedCoreTimeline = event.metadata;",
                  SDK_PLAYBACK_ADAPTER_CPP)

    eof_body = source[source.find("if (std::holds_alternative<media_sdk::EndOfFileEvent>"):
                      source.find("if (const auto* payload = std::get_if<media_sdk::PositionChangedEvent>")]
    assert_contains(eof_body,
                    "acceptedRuntimeTimelineForCoreEvent(event.metadata)",
                    SDK_PLAYBACK_ADAPTER_CPP)
    assert_not_contains(eof_body,
                        "runtimeTimeline = m_runtimeTimeline;",
                        SDK_PLAYBACK_ADAPTER_CPP)

    eos_body = source[source.find("void SdkPlaybackAdapter::onEndOfStreamPresented"):
                      source.find("bool SdkPlaybackAdapter::handleDataEvent")]
    assert_contains(eos_body, "acceptsRuntimeTimeline(timeline)", SDK_PLAYBACK_ADAPTER_CPP)

    ensure_runtime_body = source[source.find("bool SdkPlaybackAdapter::ensureRuntimeForMedia"):
                                 source.find("media_sdk::runtime::RuntimeTimeline SdkPlaybackAdapter::currentTimeline")]
    assert_contains(ensure_runtime_body,
                    "previousRuntimePlayer = std::move(m_runtimePlayer);",
                    SDK_PLAYBACK_ADAPTER_CPP)
    assert_before(ensure_runtime_body,
                  "previousRuntimePlayer = std::move(m_runtimePlayer);",
                  "m_runtimePlayer = std::move(runtimePlayer);",
                  SDK_PLAYBACK_ADAPTER_CPP)
    assert_before(ensure_runtime_body,
                  "m_runtimePlayer = std::move(runtimePlayer);",
                  "previousRuntimePlayer->stop();",
                  SDK_PLAYBACK_ADAPTER_CPP)


def check_runtime_mode_switch_contract() -> None:
    pipeline_header = read(PLAYBACK_PIPELINE_H)
    pipeline_source = read(PLAYBACK_PIPELINE_CPP)
    engine_header = read(PLAYER_ENGINE_H)
    engine_source = read(PLAYER_ENGINE_CPP)
    playplugin_cmake = read(PLAYPLUGIN_CMAKE)
    combined = "\n".join((pipeline_header, pipeline_source, engine_header, engine_source))

    for token in ("PlaybackRuntimeMode", "LegacyQt", "SdkRuntime"):
        assert_contains(combined, token, PLAYBACK_PIPELINE_H)

    assert_contains(engine_header, "playbackRuntimeMode", PLAYER_ENGINE_H)
    assert_contains(engine_source, "setPlaybackRuntimeMode", PLAYER_ENGINE_CPP)
    assert_contains(engine_source, "stop();", PLAYER_ENGINE_CPP)
    assert_contains(engine_source, "m_completion.resetForStop()", PLAYER_ENGINE_CPP)
    assert_contains(engine_source, "m_errorString.clear()", PLAYER_ENGINE_CPP)

    assert_contains(pipeline_source, "setRuntimeMode", PLAYBACK_PIPELINE_CPP)
    assert_contains(pipeline_source, "stopComponents();", PLAYBACK_PIPELINE_CPP)
    assert_contains(pipeline_source, "clearSurface();", PLAYBACK_PIPELINE_CPP)
    assert_contains(pipeline_source, "m_videoQueue.flush()", PLAYBACK_PIPELINE_CPP)
    assert_contains(pipeline_source, "m_audioQueue.flush()", PLAYBACK_PIPELINE_CPP)

    assert_contains(pipeline_source, "connectLegacySurface", PLAYBACK_PIPELINE_CPP)
    assert_contains(pipeline_source, "disconnectLegacySurface", PLAYBACK_PIPELINE_CPP)
    assert_contains(pipeline_source, "createSdkRuntimeChain", PLAYBACK_PIPELINE_CPP)
    assert_contains(pipeline_source, "destroySdkRuntimeChain", PLAYBACK_PIPELINE_CPP)
    assert_contains(pipeline_source, "SdkPlaybackAdapter", PLAYBACK_PIPELINE_CPP)
    assert_contains(pipeline_source, "QtRhiVideoPresenter", PLAYBACK_PIPELINE_CPP)
    assert_contains(pipeline_source, "media_sdk::platform::macos::CoreAudioAudioOutput", PLAYBACK_PIPELINE_CPP)
    assert_contains(playplugin_cmake, "media_sdk::platform_audio_macos", PLAYPLUGIN_CMAKE)


def main() -> None:
    check_runtime_boundary()
    check_presenter_contract()
    check_scene_graph_dependency_location()
    check_cmake_registration()
    check_sdk_playback_adapter_contract()
    check_runtime_mode_switch_contract()


if __name__ == "__main__":
    main()
