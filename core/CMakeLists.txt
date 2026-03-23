add_library(VideoPlayerCore STATIC
    PlayerEngine.h
    PlayerEngine.cpp
    PlaylistModel.h
    PlaylistModel.cpp
    MediaInfo.h
    PluginManager.h
    PluginManager.cpp
)

target_include_directories(VideoPlayerCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/plugin
    ${CMAKE_SOURCE_DIR}/logger
)

target_link_libraries(VideoPlayerCore PUBLIC
    Qt6::Core
    Qt6::Quick
    VideoPlayerLogger    # Logger 独立库
    VideoPlayerPlugin
)
