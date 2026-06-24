add_library(media_sdk_playback_runtime STATIC
    src/PlaybackRuntimeVersion.cpp
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
endif()
