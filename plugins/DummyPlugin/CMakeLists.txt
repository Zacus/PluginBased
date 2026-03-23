add_library(DummyPlugin MODULE
    DummyPlugin.h
    DummyPlugin.cpp
)

target_include_directories(DummyPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/plugin
    ${CMAKE_SOURCE_DIR}/logger
)

target_link_libraries(DummyPlugin PRIVATE
    Qt6::Core
    VideoPlayerPlugin
    VideoPlayerLogger    # 直接链独立 Logger 库，无循环依赖
)

set_target_properties(DummyPlugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)
