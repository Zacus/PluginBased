add_library(VideoPlayerLogger STATIC
    Logger.h
    Logger.cpp
)

target_include_directories(VideoPlayerLogger PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(VideoPlayerLogger PUBLIC
    Qt6::Core
    spdlog::spdlog
)
