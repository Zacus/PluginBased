#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    session_router = read("sdk/media_playback_session/src/SessionEventRouter.h")
    session_cpp = read("sdk/media_playback_session/src/PlaybackSession.cpp")
    pipeline_cpp = read("plugins/PlayPlugin/src/playback/PlaybackPipeline.cpp")

    require(not (ROOT / "plugins/PlayPlugin/src/playback/PlaybackDataBridge.h").exists(),
            "PlaybackDataBridge should be removed after SDK session owns frame routing")
    require(not (ROOT / "plugins/PlayPlugin/src/playback/QtPlaybackAdapter.h").exists(),
            "QtPlaybackAdapter should be removed after PlayPlugin switches to SdkPlaybackAdapter")
    require("handleCoreEndOfFile" in session_router and "onEndOfStreamPresented" in session_router,
            "SDK session should own EOF drain semantics")
    require("beginSeek" in session_router and "cancelFrameAcceptance" in session_router,
            "SDK session should own seek frame-acceptance semantics")
    require("PlaybackSession" in session_cpp and "RuntimePlayer" in session_cpp,
            "SDK session should compose core and runtime playback")
    require("endOfAudio" in pipeline_cpp and "endOfVideo" in pipeline_cpp,
            "PlayPlugin should only forward SDK drain completion signals")


if __name__ == "__main__":
    main()
