qt_add_executable(VideoPlayerApp
    main.cpp
    AppConfig.h
    AppConfig.cpp
    AppController.h
    AppController.cpp
    CrashHandler.h
    CrashHandler.cpp
)

# ── VideoPlayer 1.0 QML 模块（宿主基础设施层）────────────────
# SOURCES 里只放有 QML_ELEMENT 宏的类，让 qt_add_qml_module
# 为它们生成类型注册代码。
# PluginManager 已在 VideoPlayerCore (STATIC) 里编译，
# 不再重复放进 SOURCES，否则会出现重复符号。
qt_add_qml_module(VideoPlayerApp
    URI     "VideoPlayer"
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/VideoPlayer"
    QML_FILES
        qml/main.qml
        qml/HomePanel.qml
    SOURCES
        ../core/PluginManager.h         # 仅传给 moc/qmltyperegistrar 扫描类型
                                        # 实现由 VideoPlayerCore 提供，不重复编译 .cpp
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
    VideoPlayerCore                     # PluginManager 实现（含符号）
    VideoPlayerLogger
    VideoPlayerPlugin
    QtQuickComponents
    ${DUMP_LIBS}
)

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
