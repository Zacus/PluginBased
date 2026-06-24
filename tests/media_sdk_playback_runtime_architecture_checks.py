from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "sdk" / "media_playback_runtime"
ROOT_CMAKE = ROOT / "CMakeLists.txt"

FORBIDDEN_RUNTIME_TOKENS = (
    "QObject",
    "QQuickItem",
    "QSGNode",
    "QRhi",
    "QAudioSink",
    "#include <QObject",
    "#include <QQuickItem",
    "#include <QSGNode",
    "#include <QRhi",
    "#include <QAudio",
    "CoreAudioAudioOutput",
)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def assert_contains(text: str, token: str, source: Path) -> None:
    if token not in text:
        raise AssertionError(f"{source} must contain {token!r}")


def main() -> None:
    cmake = read(ROOT_CMAKE)
    assert_contains(cmake, "add_subdirectory(sdk/media_playback_runtime)", ROOT_CMAKE)
    assert_contains(cmake, "media_sdk_playback_runtime_architecture_checks", ROOT_CMAKE)

    runtime_cmake = read(RUNTIME / "CMakeLists.txt")
    assert_contains(runtime_cmake, "add_library(media_sdk_playback_runtime STATIC", RUNTIME / "CMakeLists.txt")
    assert_contains(
        runtime_cmake,
        "add_library(media_sdk::playback_runtime ALIAS media_sdk_playback_runtime)",
        RUNTIME / "CMakeLists.txt",
    )
    assert_contains(
        runtime_cmake,
        "target_compile_features(media_sdk_playback_runtime PUBLIC cxx_std_20)",
        RUNTIME / "CMakeLists.txt",
    )
    assert_contains(runtime_cmake, "AUTOMOC OFF", RUNTIME / "CMakeLists.txt")
    assert_contains(runtime_cmake, "media_sdk::core", RUNTIME / "CMakeLists.txt")

    runtime_include = RUNTIME / "include" / "media_sdk" / "runtime"
    for header_name in ("AudioOutput.h", "VideoPresenter.h", "RuntimeTypes.h"):
        header = runtime_include / header_name
        if not header.exists():
            raise AssertionError(f"{header} must exist")

    video_presenter_header = runtime_include / "VideoPresenter.h"
    video_presenter = read(video_presenter_header)
    for token in ("IVideoPresenterEvents", "PresentCompletion", "onPresentComplete", "PresentId"):
        assert_contains(video_presenter, token, video_presenter_header)

    scanned = []
    for suffix in ("*.h", "*.cpp"):
        scanned.extend(RUNTIME.rglob(suffix))
    if not scanned:
        raise AssertionError("runtime target must contain source/header files")

    for path in scanned:
        text = read(path)
        for token in FORBIDDEN_RUNTIME_TOKENS:
            if token in text:
                raise AssertionError(f"{path} contains forbidden runtime dependency token {token!r}")


if __name__ == "__main__":
    main()
