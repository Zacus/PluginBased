qt_add_executable(VideoPlayerApp
    main.cpp
    AppConfig.h
    AppConfig.cpp
    AppController.h
    AppController.cpp
    CrashHandler.h
    CrashHandler.cpp
)

qt_add_qml_module(VideoPlayerApp
    URI     "VideoPlayer"
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/VideoPlayer"
    QML_FILES
        qml/main.qml
        qml/PlayerView.qml
        qml/ControlBar.qml
        qml/PlaylistView.qml
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
    QtQuickComponents     # backing library 名，由 qt_add_library(QtQuickComponents ...) 决定，与 URI 无关
    #QtQuickComponentsplugin #qt静态库生成自动添加了plugin后缀，必须链接这个库才能使用插件
    ${DUMP_LIBS}
)

# 将构建根目录加入 QML 搜索路径，使引擎能找到 QuickUI/Components/qmldir
target_compile_definitions(VideoPlayerApp PRIVATE
    QML_IMPORT_PATH="${CMAKE_BINARY_DIR}"
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
