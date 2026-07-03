add_library(media_sdk_playback_session STATIC
    src/PlaybackSession.cpp
)

add_library(media_sdk::playback_session ALIAS media_sdk_playback_session)

target_compile_features(media_sdk_playback_session PUBLIC cxx_std_20)
set_target_properties(media_sdk_playback_session PROPERTIES
    CXX_EXTENSIONS OFF
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

target_include_directories(media_sdk_playback_session PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(media_sdk_playback_session PUBLIC
    media_sdk::core
    media_sdk::playback_runtime
)

if(BUILD_TESTING)
    add_executable(MediaSdkPlaybackSessionContractTest
        tests/tst_playback_session_contract.cpp
    )
    target_compile_features(MediaSdkPlaybackSessionContractTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkPlaybackSessionContractTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(MediaSdkPlaybackSessionContractTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(MediaSdkPlaybackSessionContractTest PRIVATE
        media_sdk_playback_session
    )
    add_test(NAME media_sdk_playback_session_contract
        COMMAND $<TARGET_FILE:MediaSdkPlaybackSessionContractTest>
    )

    add_executable(MediaSdkPlaybackSessionTimelineTest
        tests/tst_playback_session_timeline.cpp
    )
    target_compile_features(MediaSdkPlaybackSessionTimelineTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkPlaybackSessionTimelineTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(MediaSdkPlaybackSessionTimelineTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(MediaSdkPlaybackSessionTimelineTest PRIVATE
        media_sdk_playback_session
    )
    add_test(NAME media_sdk_playback_session_timeline
        COMMAND $<TARGET_FILE:MediaSdkPlaybackSessionTimelineTest>
    )

    add_executable(MediaSdkPlaybackSessionAudioConverterTest
        tests/tst_playback_session_audio_converter.cpp
    )
    target_compile_features(MediaSdkPlaybackSessionAudioConverterTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkPlaybackSessionAudioConverterTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(MediaSdkPlaybackSessionAudioConverterTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(MediaSdkPlaybackSessionAudioConverterTest PRIVATE
        media_sdk_playback_session
    )
    add_test(NAME media_sdk_playback_session_audio_converter
        COMMAND $<TARGET_FILE:MediaSdkPlaybackSessionAudioConverterTest>
    )

    add_executable(MediaSdkPlaybackSessionFrameRouterTest
        tests/tst_playback_session_frame_router.cpp
    )
    target_compile_features(MediaSdkPlaybackSessionFrameRouterTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkPlaybackSessionFrameRouterTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(MediaSdkPlaybackSessionFrameRouterTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(MediaSdkPlaybackSessionFrameRouterTest PRIVATE
        media_sdk_playback_session
    )
    add_test(NAME media_sdk_playback_session_frame_router
        COMMAND $<TARGET_FILE:MediaSdkPlaybackSessionFrameRouterTest>
    )

    add_executable(MediaSdkPlaybackSessionEventRouterTest
        tests/tst_playback_session_event_router.cpp
    )
    target_compile_features(MediaSdkPlaybackSessionEventRouterTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkPlaybackSessionEventRouterTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(MediaSdkPlaybackSessionEventRouterTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(MediaSdkPlaybackSessionEventRouterTest PRIVATE
        media_sdk_playback_session
    )
    add_test(NAME media_sdk_playback_session_event_router
        COMMAND $<TARGET_FILE:MediaSdkPlaybackSessionEventRouterTest>
    )
endif()
