qt_add_executable(VideoPlayerApp
    main.cpp
    AppConfig.h
    AppConfig.cpp
    AppController.h
    AppController.cpp
    CrashHandler.h
    CrashHandler.cpp
)

# ENABLE_EXPORTS → 链接时加 -rdynamic（Linux）/ -Wl,-export_dynamic（macOS）
# 将主程序所有非 static 符号导出到动态符号表，
# 使运行时 dlopen 的插件 .so 能直接解析 PluginManager 等宿主符号，
# 无需插件静态链接 VideoPlayerCore。
set_target_properties(VideoPlayerApp PROPERTIES
    ENABLE_EXPORTS    ON
    WIN32_EXECUTABLE  TRUE
    MACOSX_BUNDLE     TRUE
)

# ── VideoPlayer 1.0 QML 模块（宿主基础设施层）────────────────
qt_add_qml_module(VideoPlayerApp
    URI     "VideoPlayer"
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/VideoPlayer"
    QML_FILES
        qml/main.qml
        qml/HomePanel.qml
    SOURCES
        # PluginManager 在宿主层编译，符号随主程序导出供插件运行时使用
        ../core/PluginManager.h
        ../core/PluginManager.cpp
        # AppController 也是宿主 QML 单例
        AppController.h
        AppController.cpp
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
    VideoPlayerCore             # INTERFACE：传递 PluginManager.h 等头文件搜索路径
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
