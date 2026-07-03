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
endif()
