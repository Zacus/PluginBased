#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SDK_ROOT = ROOT / "sdk" / "media_core"
PUBLIC_INCLUDE = SDK_ROOT / "include" / "media_sdk"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    root_cmake = read(ROOT / "CMakeLists.txt")
    design_doc = read(ROOT / "docs" / "superpowers" / "specs" /
                      "2026-06-22-media-sdk-bplus-design.md")
    require((SDK_ROOT / "CMakeLists.txt").exists(),
            "sdk/media_core/CMakeLists.txt should exist")
    sdk_cmake = read(SDK_ROOT / "CMakeLists.txt")

    require("add_subdirectory(sdk/media_core)" in root_cmake,
            "top-level CMake should include the media SDK core target")
    require("add_test(NAME media_sdk_core_architecture_checks" in root_cmake,
            "CTest should run the media SDK core architecture check")
    require("add_library(media_sdk_core" in sdk_cmake,
            "media_sdk_core should be an independent library target")
    require("cxx_std_20" in sdk_cmake,
            "media_sdk_core should require C++20")
    require("AUTOMOC OFF" in sdk_cmake and "AUTOUIC OFF" in sdk_cmake and "AUTORCC OFF" in sdk_cmake,
            "media_sdk_core should disable Qt automoc/autouic/autorcc")
    require("ClockSync.cpp" in sdk_cmake and "FrameScheduler.cpp" in sdk_cmake,
            "media_sdk_core should compile the phase 2 primitive sources")
    require("MediaSdkCorePrimitivesTest" in sdk_cmake and "media_sdk_core_primitives" in sdk_cmake,
            "media_sdk_core should register primitive C++ tests")
    require("MediaSdkCoreFrameContractTest" in sdk_cmake and "media_sdk_core_frame_contract" in sdk_cmake,
            "media_sdk_core should register frame contract C++ tests")

    expected_headers = [
        "PlayerConfig.h",
        "Error.h",
        "Result.h",
        "Frame.h",
        "MediaEvents.h",
        "Player.h",
    ]
    for header in expected_headers:
        require((PUBLIC_INCLUDE / header).exists(),
                f"missing public SDK header: {header}")

    frame_header = read(PUBLIC_INCLUDE / "Frame.h")
    for token in ["PlaneView", "NativeHandle", "VideoFrame", "AudioFrame",
                  "std::chrono::microseconds", "std::shared_ptr<void> storage"]:
        require(token in frame_header,
                f"Frame.h should expose the phase 3 frame contract token: {token}")

    media_events_header = read(PUBLIC_INCLUDE / "MediaEvents.h")
    for token in ["MediaInfo", "PositionChangedEvent", "AudioFrameEvent",
                  "VideoFrameEvent", "EndOfFileEvent"]:
        require(token in media_events_header,
                f"MediaEvents.h should expose the phase 3 event token: {token}")

    forbidden_tokens = [
        "#include <Q",
        "#include <Qt",
        "QObject",
        "QThread",
        "QString",
        "QUrl",
        "QRhi",
        "QSG",
        "QAudio",
        "EventBus",
        "Observable",
        "template<class Derived>",
        "template<typename Derived>",
    ]
    for source in list(SDK_ROOT.rglob("*.h")) + list(SDK_ROOT.rglob("*.cpp")):
        text = read(source)
        for token in forbidden_tokens:
            require(token not in text,
                    f"{source.relative_to(ROOT)} must stay Qt-free; found {token}")

    required_design_terms = [
        "PIMPL",
        "single `IEventSink`",
        "不引入泛型 Observable",
        "不使用 CRTP",
        "command submission",
        "std::jthread",
        "std::stop_token",
    ]
    for term in required_design_terms:
        require(term in design_doc,
                f"media SDK design should document the constraint: {term}")


if __name__ == "__main__":
    main()
