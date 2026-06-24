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
        assert_contains(combined, token, PRESENTER_CPP)


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
    assert_contains(playplugin_cmake, "media_sdk::playback_runtime", PLAYPLUGIN_CMAKE)
    assert_contains(root_cmake, "playplugin_qt_rhi_presenter_architecture_checks", ROOT_CMAKE)


def main() -> None:
    check_runtime_boundary()
    check_presenter_contract()
    check_scene_graph_dependency_location()
    check_cmake_registration()


if __name__ == "__main__":
    main()
