from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "sdk" / "media_playback_runtime"
PLATFORM_AUDIO_MACOS = ROOT / "sdk" / "media_platform_audio_macos"
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
    "libavfilter",
    "AVFilter",
)

FORBIDDEN_PLATFORM_AUDIO_QUEUE_TOKENS = (
    "AudioQueue",
    "AudioQueueRef",
    "AudioQueueBufferRef",
    "AudioQueueNewOutput",
    "AudioQueueEnqueueBuffer",
    "AudioQueueStart",
    "AudioQueuePause",
    "AudioQueueReset",
    "AudioQueueStop",
    "AudioQueueDispose",
)

REQUIRED_AUDIO_UNIT_TOKENS = (
    "AudioComponentFindNext",
    "AudioComponentInstanceNew",
    "AudioUnitSetProperty",
    "kAudioUnitSubType_DefaultOutput",
    "kAudioUnitProperty_StreamFormat",
    "kAudioUnitProperty_SetRenderCallback",
    "AudioUnitInitialize",
    "AudioOutputUnitStart",
    "AudioOutputUnitStop",
    "AudioUnitReset",
    "AudioComponentInstanceDispose",
)

FORBIDDEN_PUBLIC_AUDIO_HEADER_TOKENS = (
    "<AudioUnit/",
    "<AudioToolbox/",
    "<CoreAudio/",
    "AudioUnit",
    "AudioQueue",
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
    assert_contains(cmake, "if(APPLE)", ROOT_CMAKE)
    assert_contains(cmake, "add_subdirectory(sdk/media_platform_audio_macos)", ROOT_CMAKE)

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
    for header_name in ("AudioOutput.h", "AudioTempoProcessor.h", "VideoPresenter.h", "RuntimeTypes.h"):
        header = runtime_include / header_name
        if not header.exists():
            raise AssertionError(f"{header} must exist")

    audio_output_header = runtime_include / "AudioOutput.h"
    audio_output = read(audio_output_header)
    for token in (
        "[[nodiscard",
        "Result<void> open(const AudioFormat& format)",
        "Result<void> write(AudioBufferView buffer)",
        "Result<void> resume()",
        "ClockSnapshot clock() const",
    ):
        assert_contains(audio_output, token, audio_output_header)

    video_presenter_header = runtime_include / "VideoPresenter.h"
    video_presenter = read(video_presenter_header)
    for token in (
        "IVideoPresenterEvents",
        "PresentCompletion",
        "PresentDiagnostics",
        "onPresentComplete",
        "PresentId",
    ):
        assert_contains(video_presenter, token, video_presenter_header)
    for token in (
        "nativeTextureCreated",
        "nativeTextureDrawn",
        "cpuTransferred",
        "cpuMemcpy",
        "diagnostics",
    ):
        assert_contains(video_presenter, token, video_presenter_header)
    for token in (
        "[[nodiscard",
        "Queued results must provide a non-zero PresentId",
        "VideoPresenterCapabilities capabilities() const",
        "PresentResult present(VideoFrame frame, PresentTiming timing)",
    ):
        assert_contains(video_presenter, token, video_presenter_header)

    frame_queue_header = RUNTIME / "src" / "RuntimeFrameQueue.h"
    frame_queue = read(frame_queue_header)
    for token in (
        "[[nodiscard",
        "PushResult push(FrameType frame)",
        "PushResult pushEndOfStream(SessionId sessionId, Generation generation)",
        "PopResult waitPop(FrameType& frame)",
    ):
        assert_contains(frame_queue, token, frame_queue_header)

    runtime_player_header = runtime_include / "RuntimePlayer.h"
    runtime_player = read(runtime_player_header)
    for token in (
        "RuntimeFramePushResult enqueueAudio",
        "RuntimeFramePushResult enqueueVideo",
    ):
        assert_contains(runtime_player, token, runtime_player_header)

    present_tracker_header = RUNTIME / "src" / "PresentTracker.h"
    present_tracker = read(present_tracker_header)
    for token in (
        "[[nodiscard",
        "bool track(TrackedPresent present)",
        "PresentCompletionAction complete(SessionId sessionId, Generation generation, PresentCompletion completion)",
    ):
        assert_contains(present_tracker, token, present_tracker_header)

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

    platform_cmake_path = PLATFORM_AUDIO_MACOS / "CMakeLists.txt"
    platform_cmake = read(platform_cmake_path)
    assert_contains(platform_cmake, "add_library(media_sdk_platform_audio_macos STATIC", platform_cmake_path)
    assert_contains(
        platform_cmake,
        "add_library(media_sdk::platform_audio_macos ALIAS media_sdk_platform_audio_macos)",
        platform_cmake_path,
    )
    assert_contains(platform_cmake, "media_sdk::playback_runtime", platform_cmake_path)
    assert_contains(platform_cmake, "-framework CoreAudio", platform_cmake_path)
    assert_contains(platform_cmake, "-framework AudioToolbox", platform_cmake_path)

    concrete_header = PLATFORM_AUDIO_MACOS / "include" / "media_sdk" / "platform" / "macos" / "CoreAudioAudioOutput.h"
    if not concrete_header.exists():
        raise AssertionError(f"{concrete_header} must exist")
    concrete_audio_output = read(concrete_header)
    for token in (
        "[[nodiscard",
        "Result<void> open(const runtime::AudioFormat& format) override",
        "Result<void> write(runtime::AudioBufferView buffer) override",
        "runtime::ClockSnapshot clock() const override",
    ):
        assert_contains(concrete_audio_output, token, concrete_header)
    for token in FORBIDDEN_PUBLIC_AUDIO_HEADER_TOKENS:
        if token in concrete_audio_output:
            raise AssertionError(
                f"{concrete_header} must not expose macOS audio implementation token {token!r}"
            )

    platform_audio_sources = []
    for suffix in ("*.h", "*.cpp"):
        platform_audio_sources.extend(PLATFORM_AUDIO_MACOS.rglob(suffix))
    if not platform_audio_sources:
        raise AssertionError(f"{PLATFORM_AUDIO_MACOS} must contain source/header files")

    for path in platform_audio_sources:
        text = read(path)
        for token in FORBIDDEN_PLATFORM_AUDIO_QUEUE_TOKENS:
            if token in text:
                raise AssertionError(
                    f"{path} must not use AudioQueue after AudioUnit migration, found {token!r}"
                )

    audio_unit_device_path = PLATFORM_AUDIO_MACOS / "src" / "MacAudioUnitRenderDevice.cpp"
    if not audio_unit_device_path.exists():
        raise AssertionError(f"{audio_unit_device_path} must exist")
    audio_unit_device_source = read(audio_unit_device_path)
    for token in REQUIRED_AUDIO_UNIT_TOKENS:
        assert_contains(audio_unit_device_source, token, audio_unit_device_path)
    for token in (
        "std::atomic<AudioRenderCallback::Function> m_callbackFunction",
        "std::atomic<void*> m_callbackContext",
    ):
        assert_contains(audio_unit_device_source, token, audio_unit_device_path)
    if "AudioRenderCallback m_callback" in audio_unit_device_source:
        raise AssertionError(
            f"{audio_unit_device_path} must not share non-atomic callback state with the AudioUnit render callback"
        )

    ring_buffer_header = PLATFORM_AUDIO_MACOS / "src" / "CoreAudioRingBuffer.h"
    ring_buffer_source = PLATFORM_AUDIO_MACOS / "src" / "CoreAudioRingBuffer.cpp"
    ring_buffer = read(ring_buffer_header)
    ring_buffer_impl = read(ring_buffer_source)
    for token in (
        "SPSC PCM ring buffer",
        "read() is lock-free",
        "complete PCM frames",
        "#include <atomic>",
        "std::vector<std::byte> m_buffer",
        "m_readCursor",
        "m_writeCursor",
        "m_epoch",
        "m_generation",
        "m_closed",
        "beginControlUpdate",
        "endControlUpdate",
        "bytesPerFrame",
        "completeFrameBytes",
        "copyIntoRing",
        "copyFromRing",
        "[[nodiscard",
        "bool write(runtime::AudioBufferView buffer)",
        "CoreAudioRingBufferReadResult read(std::span<std::byte> destination)",
        "runtime::ClockSnapshot clock() const",
    ):
        assert_contains(ring_buffer, token, ring_buffer_header)
    for token in (
        "#include <deque>",
        "#include <condition_variable>",
        "std::deque",
        ".push_back(",
        ".pop_front(",
        "m_notFull",
    ):
        if token in ring_buffer:
            raise AssertionError(
                f"{ring_buffer_header} should use fixed-capacity vector ring operations, found {token!r}"
            )
    write_start = ring_buffer_impl.find("bool CoreAudioRingBuffer::write")
    write_end = ring_buffer_impl.find("CoreAudioRingBufferReadResult CoreAudioRingBuffer::read")
    if write_start < 0 or write_end < 0:
        raise AssertionError(f"{ring_buffer_source} should define write() before read()")
    write_body = ring_buffer_impl[write_start:write_end]
    for token in (
        "std::atomic_wait_explicit",
        "m_wakeupSequence",
    ):
        assert_contains(write_body, token, ring_buffer_source)
    for token in (
        "wait_for",
        "std::chrono::milliseconds { 1 }",
        "std::condition_variable",
        "m_notFull",
    ):
        if token in write_body:
            raise AssertionError(
                f"{ring_buffer_source} write() must not poll for producer wakeups, found {token!r}"
            )
    read_start = ring_buffer_impl.find("CoreAudioRingBufferReadResult CoreAudioRingBuffer::read")
    read_end = ring_buffer_impl.find("void CoreAudioRingBuffer::flush")
    if read_start < 0 or read_end < 0:
        raise AssertionError(f"{ring_buffer_source} should define read() before flush()")
    read_body = ring_buffer_impl[read_start:read_end]
    for token in (
        "epoch % 2 != 0",
        "epoch != m_epoch.load",
    ):
        assert_contains(read_body, token, ring_buffer_source)
    for token in (
        "std::scoped_lock",
        "std::unique_lock",
        ".lock(",
        ".wait(",
        ".notify_",
        "m_mutex",
    ):
        if token in read_body:
            raise AssertionError(
                f"{ring_buffer_source} read() must be lock-free for CoreAudio callbacks, found {token!r}"
            )
    flush_body = ring_buffer_impl[read_end:ring_buffer_impl.find("void CoreAudioRingBuffer::close")]
    require_order = (
        flush_body.find("beginControlUpdate();"),
        flush_body.find("resetCursors();"),
        flush_body.find("endControlUpdate();"),
    )
    if not (0 <= require_order[0] < require_order[1] < require_order[2]):
        raise AssertionError(
            f"{ring_buffer_source} flush() must publish seqlock epoch after cursor reset"
        )
    clock_start = ring_buffer_impl.find("runtime::ClockSnapshot CoreAudioRingBuffer::clock")
    clock_end = ring_buffer_impl.find("std::size_t CoreAudioRingBuffer::bytesPerSample")
    if clock_start < 0 or clock_end < 0:
        raise AssertionError(f"{ring_buffer_source} should define clock() before bytesPerSample()")
    clock_body = ring_buffer_impl[clock_start:clock_end]
    for token in (
        "epoch % 2 != 0",
        "epoch != m_epoch.load",
        ".valid = false",
    ):
        assert_contains(clock_body, token, ring_buffer_source)
    for token in (
        "std::scoped_lock",
        "std::unique_lock",
        ".lock(",
        ".wait(",
        "m_mutex",
    ):
        if token in clock_body:
            raise AssertionError(
                f"{ring_buffer_source} clock() must use lock-free seqlock snapshots, found {token!r}"
            )

    concrete_include = '#include "media_sdk/platform/macos/CoreAudioAudioOutput.h"'
    for path in scanned:
        text = read(path)
        if concrete_include in text:
            raise AssertionError(f"{path} must not include the concrete CoreAudio output header")


if __name__ == "__main__":
    main()
