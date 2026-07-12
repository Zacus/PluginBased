cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(MediaSdkVideoBenchmarkTools LANGUAGES CXX)

    set(PLUGINBASED_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH
        "PluginBased source tree whose media SDK should be benchmarked")
    get_filename_component(PLUGINBASED_SOURCE_DIR "${PLUGINBASED_SOURCE_DIR}" ABSOLUTE)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
    pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
    pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
    pkg_check_modules(SWSCALE REQUIRED IMPORTED_TARGET libswscale)
    pkg_check_modules(SWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)

    if(APPLE)
        foreach(_ffmpeg_target
                PkgConfig::AVFORMAT
                PkgConfig::AVCODEC
                PkgConfig::AVUTIL
                PkgConfig::SWSCALE
                PkgConfig::SWRESAMPLE)
            set_property(TARGET ${_ffmpeg_target} PROPERTY INTERFACE_LINK_OPTIONS "")
        endforeach()
    endif()

    add_library(FFmpeg::all INTERFACE IMPORTED)
    target_link_libraries(FFmpeg::all INTERFACE
        PkgConfig::AVFORMAT
        PkgConfig::AVCODEC
        PkgConfig::AVUTIL
        PkgConfig::SWSCALE
        PkgConfig::SWRESAMPLE
    )

    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    add_subdirectory("${PLUGINBASED_SOURCE_DIR}/sdk/media_core"
                     "${CMAKE_BINARY_DIR}/media_core")
    add_subdirectory("${PLUGINBASED_SOURCE_DIR}/sdk/media_playback_runtime"
                     "${CMAKE_BINARY_DIR}/media_playback_runtime")
    add_subdirectory("${PLUGINBASED_SOURCE_DIR}/sdk/media_playback_session"
                     "${CMAKE_BINARY_DIR}/media_playback_session")
else()
    set(PLUGINBASED_SOURCE_DIR "${CMAKE_SOURCE_DIR}")
endif()

add_executable(MediaSdkVideoBenchmark
    MediaSdkVideoBenchmark.cpp
)
target_compile_features(MediaSdkVideoBenchmark PRIVATE cxx_std_20)
set_target_properties(MediaSdkVideoBenchmark PROPERTIES
    CXX_EXTENSIONS OFF
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_link_libraries(MediaSdkVideoBenchmark PRIVATE media_sdk_core)

if(EXISTS "${PLUGINBASED_SOURCE_DIR}/sdk/media_core/include/media_sdk/Diagnostics.h")
    target_compile_definitions(MediaSdkVideoBenchmark PRIVATE
        MEDIA_SDK_BENCHMARK_HAS_POOL_DIAGNOSTICS=1)
endif()

add_executable(MediaSdkRealtimeVideoBenchmark
    MediaSdkRealtimeVideoBenchmark.cpp
)
target_compile_features(MediaSdkRealtimeVideoBenchmark PRIVATE cxx_std_20)
set_target_properties(MediaSdkRealtimeVideoBenchmark PROPERTIES
    CXX_EXTENSIONS OFF
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_link_libraries(MediaSdkRealtimeVideoBenchmark PRIVATE media_sdk_playback_session)
if(EXISTS "${PLUGINBASED_SOURCE_DIR}/sdk/media_core/include/media_sdk/Diagnostics.h")
    target_compile_definitions(MediaSdkRealtimeVideoBenchmark PRIVATE
        MEDIA_SDK_BENCHMARK_HAS_POOL_DIAGNOSTICS=1)
endif()

add_executable(MediaSdkBenchmarkMediaGenerator
    MediaSdkBenchmarkMediaGenerator.cpp
)
target_compile_features(MediaSdkBenchmarkMediaGenerator PRIVATE cxx_std_20)
set_target_properties(MediaSdkBenchmarkMediaGenerator PROPERTIES
    CXX_EXTENSIONS OFF
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_link_libraries(MediaSdkBenchmarkMediaGenerator PRIVATE FFmpeg::all)
