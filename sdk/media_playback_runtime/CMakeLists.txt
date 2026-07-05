add_library(media_sdk_playback_runtime STATIC
    src/AvSyncScheduler.cpp
    src/MasterClock.cpp
    src/NativeFallbackController.cpp
    src/PlaybackRuntimeVersion.cpp
    src/PresentTracker.cpp
    src/RuntimePlayer.cpp
)

add_library(media_sdk::playback_runtime ALIAS media_sdk_playback_runtime)

target_compile_features(media_sdk_playback_runtime PUBLIC cxx_std_20)
set_target_properties(media_sdk_playback_runtime PROPERTIES
    CXX_EXTENSIONS OFF
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

target_include_directories(media_sdk_playback_runtime PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(media_sdk_playback_runtime PUBLIC
    media_sdk::core
)

if(BUILD_TESTING)
    add_executable(MediaSdkPlaybackRuntimeInterfaceContractTest
        tests/tst_runtime_interface_contract.cpp
    )
    target_compile_features(MediaSdkPlaybackRuntimeInterfaceContractTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkPlaybackRuntimeInterfaceContractTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_link_libraries(MediaSdkPlaybackRuntimeInterfaceContractTest PRIVATE
        media_sdk_playback_runtime
    )
    add_test(NAME media_sdk_playback_runtime_interface_contract
        COMMAND $<TARGET_FILE:MediaSdkPlaybackRuntimeInterfaceContractTest>
    )

    add_executable(MediaSdkPlaybackRuntimeFrameQueueTest
        tests/tst_runtime_frame_queue.cpp
    )
    target_compile_features(MediaSdkPlaybackRuntimeFrameQueueTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkPlaybackRuntimeFrameQueueTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(MediaSdkPlaybackRuntimeFrameQueueTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(MediaSdkPlaybackRuntimeFrameQueueTest PRIVATE
        media_sdk_playback_runtime
    )
    add_test(NAME media_sdk_playback_runtime_frame_queue
        COMMAND $<TARGET_FILE:MediaSdkPlaybackRuntimeFrameQueueTest>
    )

    add_executable(MediaSdkPlaybackRuntimeAvSyncSchedulerTest
        tests/tst_av_sync_scheduler.cpp
    )
    target_compile_features(MediaSdkPlaybackRuntimeAvSyncSchedulerTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkPlaybackRuntimeAvSyncSchedulerTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(MediaSdkPlaybackRuntimeAvSyncSchedulerTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(MediaSdkPlaybackRuntimeAvSyncSchedulerTest PRIVATE
        media_sdk_playback_runtime
    )
    add_test(NAME media_sdk_playback_runtime_av_sync_scheduler
        COMMAND $<TARGET_FILE:MediaSdkPlaybackRuntimeAvSyncSchedulerTest>
    )

    add_executable(MediaSdkPlaybackRuntimePresentTrackerTest
        tests/tst_present_tracker.cpp
    )
    target_compile_features(MediaSdkPlaybackRuntimePresentTrackerTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkPlaybackRuntimePresentTrackerTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(MediaSdkPlaybackRuntimePresentTrackerTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(MediaSdkPlaybackRuntimePresentTrackerTest PRIVATE
        media_sdk_playback_runtime
    )
    add_test(NAME media_sdk_playback_runtime_present_tracker
        COMMAND $<TARGET_FILE:MediaSdkPlaybackRuntimePresentTrackerTest>
    )

    add_executable(MediaSdkPlaybackRuntimeNativeFallbackControllerTest
        tests/tst_native_fallback_controller.cpp
    )
    target_compile_features(MediaSdkPlaybackRuntimeNativeFallbackControllerTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkPlaybackRuntimeNativeFallbackControllerTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(MediaSdkPlaybackRuntimeNativeFallbackControllerTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(MediaSdkPlaybackRuntimeNativeFallbackControllerTest PRIVATE
        media_sdk_playback_runtime
    )
    add_test(NAME media_sdk_playback_runtime_native_fallback_controller
        COMMAND $<TARGET_FILE:MediaSdkPlaybackRuntimeNativeFallbackControllerTest>
    )

    add_executable(MediaSdkPlaybackRuntimeAudioSilenceTest
        tests/tst_runtime_audio_silence.cpp
    )
    target_compile_features(MediaSdkPlaybackRuntimeAudioSilenceTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkPlaybackRuntimeAudioSilenceTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(MediaSdkPlaybackRuntimeAudioSilenceTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(MediaSdkPlaybackRuntimeAudioSilenceTest PRIVATE
        media_sdk_playback_runtime
    )
    add_test(NAME media_sdk_playback_runtime_audio_silence
        COMMAND $<TARGET_FILE:MediaSdkPlaybackRuntimeAudioSilenceTest>
    )

    add_executable(MediaSdkPlaybackRuntimeSeekClockAnchorTest
        tests/tst_seek_clock_anchor.cpp
    )
    target_compile_features(MediaSdkPlaybackRuntimeSeekClockAnchorTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkPlaybackRuntimeSeekClockAnchorTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(MediaSdkPlaybackRuntimeSeekClockAnchorTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(MediaSdkPlaybackRuntimeSeekClockAnchorTest PRIVATE
        media_sdk_playback_runtime
    )
    add_test(NAME media_sdk_playback_runtime_seek_clock_anchor
        COMMAND $<TARGET_FILE:MediaSdkPlaybackRuntimeSeekClockAnchorTest>
    )

    add_executable(MediaSdkPlaybackRuntimePlayerMockTest
        tests/tst_runtime_player_mock.cpp
    )
    target_compile_features(MediaSdkPlaybackRuntimePlayerMockTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkPlaybackRuntimePlayerMockTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_link_libraries(MediaSdkPlaybackRuntimePlayerMockTest PRIVATE
        media_sdk_playback_runtime
    )
    add_test(NAME media_sdk_playback_runtime_player_mock
        COMMAND $<TARGET_FILE:MediaSdkPlaybackRuntimePlayerMockTest>
    )
endif()
