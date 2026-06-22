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

    expected_headers = [
        "PlayerConfig.h",
        "Error.h",
        "Result.h",
        "MediaEvents.h",
        "Player.h",
    ]
    for header in expected_headers:
        require((PUBLIC_INCLUDE / header).exists(),
                f"missing public SDK header: {header}")

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
    ]
    for source in list(SDK_ROOT.rglob("*.h")) + list(SDK_ROOT.rglob("*.cpp")):
        text = read(source)
        for token in forbidden_tokens:
            require(token not in text,
                    f"{source.relative_to(ROOT)} must stay Qt-free; found {token}")


if __name__ == "__main__":
    main()
