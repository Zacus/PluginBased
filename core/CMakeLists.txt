# VideoPlayerCore 只做接口头文件的传递，实际编译由 qt_add_qml_module 的 SOURCES 完成
# 保留这个 target 是为了让 DummyPlugin 能通过它拿到头文件路径
add_library(VideoPlayerCore INTERFACE)

target_include_directories(VideoPlayerCore INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/plugin
    ${CMAKE_SOURCE_DIR}/logger
)

target_link_libraries(VideoPlayerCore INTERFACE
    Qt6::Core
    Qt6::Quick
    VideoPlayerLogger
    VideoPlayerPlugin
)
