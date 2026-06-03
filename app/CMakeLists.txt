qt_add_executable(VideoPlayerApp
    main.cpp
    AppConfig.h
    AppConfig.cpp
    CrashHandler.h
    CrashHandler.cpp
)

# ENABLE_EXPORTS → 链接时加 -rdynamic（Linux）/ -Wl,-export_dynamic（macOS）
# 将主程序所有非 static 符号导出到动态符号表，
# 使运行时 dlopen 的插件 .so 能直接解析 PluginManager 等宿主符号。
set_target_properties(VideoPlayerApp PROPERTIES
    ENABLE_EXPORTS    ON
    WIN32_EXECUTABLE  TRUE
    MACOSX_BUNDLE     TRUE
)

# ── VideoPlayer 1.0 QML 模块（宿主基础设施层）────────────────────
# SOURCES 放有 QML_ELEMENT/QML_SINGLETON 宏的类，让 qmltyperegistrar 生成类型注册代码。
# AppController 和 PluginManager 都是 QML_ELEMENT，放在这里编译一次即可。
qt_add_qml_module(VideoPlayerApp
    URI     "VideoPlayer"
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/VideoPlayer"
    QML_FILES
        qml/main.qml
        qml/HomePanel.qml
    SOURCES
        AppController.h     AppController.cpp
        ../core/PluginManager.h  ../core/PluginManager.cpp
)

target_include_directories(VideoPlayerApp PRIVATE
    ${CMAKE_SOURCE_DIR}/core        # PluginManager.h
    ${CMAKE_SOURCE_DIR}/plugin      # IAppPlugin.h
    # logger/ 头文件已由 VideoPlayerLogger PUBLIC include_directories 传递，无需手动列出
)

target_link_libraries(VideoPlayerApp PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
    VideoPlayerLogger               # Logger + AppLog QML 模块
    VideoPlayerPlugin               # IAppPlugin 接口（INTERFACE）
    VideoPlayerCore                 # PluginManager.h 头文件路径（INTERFACE）
    QtQuickComponents
    ${DUMP_LIBS}
)

# CMAKE_BINARY_DIR 下同时存在：
#   VideoPlayer/qmldir   宿主模块 (VideoPlayer 1.0)
#   PlayPlugin/qmldir    插件模块 (PlayPlugin 1.0)
#   AppLog/qmldir        日志模块 (AppLog 1.0)
# addImportPath 一次覆盖所有模块
target_compile_definitions(VideoPlayerApp PRIVATE
    QML_IMPORT_PATH="${CMAKE_BINARY_DIR}"
)

if(WIN32)
    target_compile_definitions(VideoPlayerApp PRIVATE PLATFORM_WINDOWS)
elseif(UNIX)
    target_compile_definitions(VideoPlayerApp PRIVATE PLATFORM_LINUX)
endif()

# # ── 安装规则 ────────────────────────────────────────────────────────────────
# # cmake --install 时写入 CMAKE_INSTALL_RPATH，自动替换构建期 rpath，
# # 无需事后 install_name_tool（修改二进制会破坏代码签名）。
# if(APPLE)
#     # macOS：整个 .app bundle 安装到 prefix 根目录
#     install(TARGETS VideoPlayerApp
#         BUNDLE DESTINATION .
#     )
#     # QML 模块目录 → Contents/Resources/qml/
#     # main.cpp 在 bundle 下搜索 Contents/Resources/qml/ 作为 import path
#     foreach(mod AppLog VideoPlayer PlayPlugin)
#         install(DIRECTORY "${CMAKE_BINARY_DIR}/${mod}"
#             DESTINATION "$<TARGET_BUNDLE_DIR:VideoPlayerApp>/Contents/Resources/qml"
#         )
#     endforeach()
#     # QuickUI（QtQuickComponents）QML 模块目录
#     install(DIRECTORY "${CMAKE_BINARY_DIR}/QuickUI"
#         DESTINATION "$<TARGET_BUNDLE_DIR:VideoPlayerApp>/Contents/Resources/qml"
#     )
# else()
#     # Linux / Windows：可执行文件安装到 bin/
#     install(TARGETS VideoPlayerApp
#         RUNTIME DESTINATION bin
#     )
#     # QML 模块目录 → bin/qml/（main.cpp 搜索 appDir + "/qml"）
#     foreach(mod AppLog VideoPlayer PlayPlugin)
#         install(DIRECTORY "${CMAKE_BINARY_DIR}/${mod}"
#             DESTINATION "bin/qml"
#         )
#     endforeach()
#     install(DIRECTORY "${CMAKE_BINARY_DIR}/QuickUI"
#         DESTINATION "bin/qml"
#     )
# endif()
