add_library(media_sdk_audio_ffmpeg STATIC
    src/FfmpegAudioTempoProcessor.cpp
)

add_library(media_sdk::audio_ffmpeg ALIAS media_sdk_audio_ffmpeg)

target_compile_features(media_sdk_audio_ffmpeg PUBLIC cxx_std_20)
set_target_properties(media_sdk_audio_ffmpeg PROPERTIES
    CXX_EXTENSIONS OFF
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

target_include_directories(media_sdk_audio_ffmpeg PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(media_sdk_audio_ffmpeg
    PUBLIC media_sdk::playback_runtime
    PRIVATE FFmpeg::avfilter
)

if(BUILD_TESTING)
    add_executable(MediaSdkAudioFfmpegTempoProcessorTest
        tests/tst_ffmpeg_audio_tempo_processor.cpp
    )
    target_compile_features(MediaSdkAudioFfmpegTempoProcessorTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkAudioFfmpegTempoProcessorTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_link_libraries(MediaSdkAudioFfmpegTempoProcessorTest PRIVATE
        media_sdk_audio_ffmpeg
    )
    add_test(NAME media_sdk_audio_ffmpeg_tempo_processor
        COMMAND $<TARGET_FILE:MediaSdkAudioFfmpegTempoProcessorTest>
    )
endif()
