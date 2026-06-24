add_library(media_sdk_platform_audio_macos STATIC
    src/CoreAudioAudioOutput.cpp
)

add_library(media_sdk::platform_audio_macos ALIAS media_sdk_platform_audio_macos)

target_compile_features(media_sdk_platform_audio_macos PUBLIC cxx_std_20)
set_target_properties(media_sdk_platform_audio_macos PROPERTIES
    CXX_EXTENSIONS OFF
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

target_include_directories(media_sdk_platform_audio_macos PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(media_sdk_platform_audio_macos PUBLIC
    media_sdk::playback_runtime
)

target_link_libraries(media_sdk_platform_audio_macos PRIVATE
    "-framework CoreAudio"
    "-framework AudioToolbox"
)

if(BUILD_TESTING)
    add_executable(MediaSdkPlatformAudioMacosContractTest
        tests/tst_core_audio_output_contract.cpp
    )
    target_compile_features(MediaSdkPlatformAudioMacosContractTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkPlatformAudioMacosContractTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_link_libraries(MediaSdkPlatformAudioMacosContractTest PRIVATE
        media_sdk_platform_audio_macos
    )
    add_test(NAME media_sdk_platform_audio_macos_contract
        COMMAND $<TARGET_FILE:MediaSdkPlatformAudioMacosContractTest>
    )

    add_executable(MediaSdkPlatformAudioMacosRingBufferTest
        tests/tst_core_audio_ring_buffer.cpp
    )
    target_compile_features(MediaSdkPlatformAudioMacosRingBufferTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkPlatformAudioMacosRingBufferTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(MediaSdkPlatformAudioMacosRingBufferTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(MediaSdkPlatformAudioMacosRingBufferTest PRIVATE
        media_sdk_platform_audio_macos
    )
    add_test(NAME media_sdk_platform_audio_macos_ring_buffer
        COMMAND $<TARGET_FILE:MediaSdkPlatformAudioMacosRingBufferTest>
    )
endif()
