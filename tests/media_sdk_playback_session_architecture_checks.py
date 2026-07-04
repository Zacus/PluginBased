from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SESSION = ROOT / "sdk" / "media_playback_session"
ROOT_CMAKE = ROOT / "CMakeLists.txt"

FORBIDDEN_PUBLIC_TOKENS = (
    "QObject",
    "QQuick",
    "QSG",
    "QRhi",
    "#include <Q",
)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def assert_contains(text: str, token: str, source: Path) -> None:
    if token not in text:
        raise AssertionError(f"{source} must contain {token!r}")


def assert_not_contains(text: str, token: str, source: Path) -> None:
    if token in text:
        raise AssertionError(f"{source} must not contain {token!r}")


def main() -> None:
    root_cmake = read(ROOT_CMAKE)
    assert_contains(root_cmake, "add_subdirectory(sdk/media_playback_session)", ROOT_CMAKE)
    assert_contains(root_cmake, "media_sdk_playback_session_architecture_checks", ROOT_CMAKE)

    cmake = SESSION / "CMakeLists.txt"
    header = SESSION / "include" / "media_sdk" / "session" / "PlaybackSession.h"
    types = SESSION / "include" / "media_sdk" / "session" / "PlaybackSessionTypes.h"
    source = SESSION / "src" / "PlaybackSession.cpp"

    for path in (cmake, header, types, source):
        if not path.exists():
            raise AssertionError(f"{path} must exist")

    cmake_text = read(cmake)
    for token in (
        "add_library(media_sdk_playback_session STATIC",
        "add_library(media_sdk::playback_session ALIAS media_sdk_playback_session)",
        "target_compile_features(media_sdk_playback_session PUBLIC cxx_std_20)",
        "AUTOMOC OFF",
        "media_sdk::core",
        "media_sdk::playback_runtime",
    ):
        assert_contains(cmake_text, token, cmake)

    for path in (header, types):
        text = read(path)
        assert_contains(text, "namespace media_sdk::session", path)
        for token in FORBIDDEN_PUBLIC_TOKENS:
            assert_not_contains(text, token, path)

    header_text = read(header)
    for token in (
        "class PlaybackSession final",
        "Result<void> open(const std::filesystem::path& path)",
        "Result<void> seek(std::chrono::milliseconds position)",
        "runtime::RuntimeTimeline timeline() const",
        "runtime::RuntimeDiagnostics diagnostics() const",
    ):
        assert_contains(header_text, token, header)

    types_text = read(types)
    for token in (
        "class ISessionEvents",
        "onRuntimeDiagnostics(runtime::RuntimeDiagnostics diagnostics)",
        "struct PlaybackSessionConfig",
        "struct PlaybackSessionDependencies",
        "runtime::IAudioOutput* audioOutput",
        "runtime::IVideoPresenter* videoPresenter",
    ):
        assert_contains(types_text, token, types)


if __name__ == "__main__":
    main()
