add_library(media_sdk_core STATIC
    src/DecodePerformance.cpp
    src/Demuxer.cpp
    src/FFmpegUtils.h
    src/HardwareDecoderBackend.h
    src/HardwareDecoderFactory.cpp
    src/HardwareDecoderFactory.h
    src/Player.cpp
    src/PlaybackController.cpp
    src/DecodeWorker.cpp
    src/QueuePolicy.h
    src/StreamDecoder.cpp
    src/VideoToolboxBackend.cpp
    src/VideoToolboxBackend.h
    src/VideoFrameProcessor.cpp
)

add_library(media_sdk::core ALIAS media_sdk_core)

target_compile_features(media_sdk_core PUBLIC cxx_std_20)
set_target_properties(media_sdk_core PROPERTIES
    CXX_EXTENSIONS OFF
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

target_include_directories(media_sdk_core PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(media_sdk_core PRIVATE
    FFmpeg::all
)

if(BUILD_TESTING)
    add_executable(MediaSdkCorePrimitivesTest
        tests/tst_media_core_primitives.cpp
    )
    target_compile_features(MediaSdkCorePrimitivesTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkCorePrimitivesTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(MediaSdkCorePrimitivesTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(MediaSdkCorePrimitivesTest PRIVATE media_sdk_core)
    add_test(NAME media_sdk_core_primitives
        COMMAND $<TARGET_FILE:MediaSdkCorePrimitivesTest>
    )

    add_executable(MediaSdkCoreFrameContractTest
        tests/tst_media_core_frame_contract.cpp
    )
    target_compile_features(MediaSdkCoreFrameContractTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkCoreFrameContractTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(MediaSdkCoreFrameContractTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_link_libraries(MediaSdkCoreFrameContractTest PRIVATE media_sdk_core)
    add_test(NAME media_sdk_core_frame_contract
        COMMAND $<TARGET_FILE:MediaSdkCoreFrameContractTest>
    )

    add_executable(MediaSdkCoreFfmpegIntegrationTest
        tests/tst_media_core_ffmpeg_integration.cpp
    )
    target_compile_features(MediaSdkCoreFfmpegIntegrationTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkCoreFfmpegIntegrationTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(MediaSdkCoreFfmpegIntegrationTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(MediaSdkCoreFfmpegIntegrationTest PRIVATE media_sdk_core FFmpeg::all)
    add_test(NAME media_sdk_core_ffmpeg_integration
        COMMAND $<TARGET_FILE:MediaSdkCoreFfmpegIntegrationTest>
    )

    add_executable(MediaSdkCoreVideoFrameProcessorTest
        tests/tst_media_core_video_frame_processor.cpp
    )
    target_compile_features(MediaSdkCoreVideoFrameProcessorTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkCoreVideoFrameProcessorTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(MediaSdkCoreVideoFrameProcessorTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(MediaSdkCoreVideoFrameProcessorTest PRIVATE media_sdk_core FFmpeg::all)
    add_test(NAME media_sdk_core_video_frame_processor
        COMMAND $<TARGET_FILE:MediaSdkCoreVideoFrameProcessorTest>
    )

    add_executable(MediaSdkCorePlaybackWorkerTest
        tests/tst_media_core_playback_worker.cpp
    )
    target_compile_features(MediaSdkCorePlaybackWorkerTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkCorePlaybackWorkerTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(MediaSdkCorePlaybackWorkerTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(MediaSdkCorePlaybackWorkerTest PRIVATE media_sdk_core FFmpeg::all)
    add_test(NAME media_sdk_core_playback_worker
        COMMAND $<TARGET_FILE:MediaSdkCorePlaybackWorkerTest>
    )

    add_executable(MediaSdkCoreSeekPrerollGateTest
        tests/tst_media_core_seek_preroll_gate.cpp
    )
    target_compile_features(MediaSdkCoreSeekPrerollGateTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkCoreSeekPrerollGateTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(MediaSdkCoreSeekPrerollGateTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(MediaSdkCoreSeekPrerollGateTest PRIVATE media_sdk_core)
    add_test(NAME media_sdk_core_seek_preroll_gate
        COMMAND $<TARGET_FILE:MediaSdkCoreSeekPrerollGateTest>
    )
endif()
