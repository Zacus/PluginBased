qt_add_executable(VideoPlayerApp
    main.cpp
    AppController.h
    AppController.cpp
    CrashHandler.h
    CrashHandler.cpp
)

# Qt6 qt_add_qml_module 规则：
#   如果把 qml/components/IconButton.qml 直接放入 URI "VideoPlayer" 模块，
#   Qt6 会按路径前缀注册为 "components/IconButton"，导致 "IconButton is not a type"。
#
# 解决方案：QT_RESOURCE_ALIAS
#   将 components 下的文件在资源系统里别名为模块根级路径，
#   Qt6 就将其类型名识别为干净的 "IconButton" / "ProgressSlider"。

set_source_files_properties(qml/components/IconButton.qml PROPERTIES
    QT_RESOURCE_ALIAS "qml/IconButton.qml"
)
set_source_files_properties(qml/components/ProgressSlider.qml PROPERTIES
    QT_RESOURCE_ALIAS "qml/ProgressSlider.qml"
)

qt_add_qml_module(VideoPlayerApp
    URI     "VideoPlayer"
    VERSION 1.0
    QML_FILES
        qml/main.qml
        qml/PlayerView.qml
        qml/ControlBar.qml
        qml/PlaylistView.qml
        qml/components/IconButton.qml
        qml/components/ProgressSlider.qml
    SOURCES
        ../core/PlayerEngine.h      ../core/PlayerEngine.cpp
        ../core/PlaylistModel.h     ../core/PlaylistModel.cpp
        ../core/MediaInfo.h
        ../core/PluginManager.h     ../core/PluginManager.cpp
)

target_include_directories(VideoPlayerApp PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/plugin
    ${CMAKE_SOURCE_DIR}/logger
)

target_link_libraries(VideoPlayerApp PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
    VideoPlayerLogger
    VideoPlayerPlugin
    ${DUMP_LIBS}
)

if(WIN32)
    target_compile_definitions(VideoPlayerApp PRIVATE PLATFORM_WINDOWS)
elseif(UNIX)
    target_compile_definitions(VideoPlayerApp PRIVATE PLATFORM_LINUX)
endif()

set_target_properties(VideoPlayerApp PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE    TRUE
)
