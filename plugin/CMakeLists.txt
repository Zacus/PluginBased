add_library(VideoPlayerPlugin INTERFACE)
target_include_directories(VideoPlayerPlugin INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(VideoPlayerPlugin INTERFACE Qt6::Core)
