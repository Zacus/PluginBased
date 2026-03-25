# ============================================================
# PlayPlugin —— 完全自包含的播放器插件
#
# Step 1 后的依赖关系：
#
#  VideoPlayerApp（主程序，ENABLE_EXPORTS=ON，-rdynamic）
#    ├─ 编译：PluginManager.cpp → 符号进入动态符号表
#    └─ dlopen → PlayPlugin.so
#                 ├─ 静态链接：PlayPluginBackend（PlayerEngine 等）
#                 ├─ 静态链接：VideoPlayerPlugin（IPlayerPlugin 接口头文件）
#                 ├─ 静态链接：VideoPlayerLogger
#                 └─ 运行时解析：PluginManager 符号（从主程序动态符号表）
#                    → 不再静态链接 VideoPlayerCore，消除插件→宿主的静态依赖
#
# -Wl,--allow-shlib-undefined：
#   告知链接器允许 MODULE .so 中存在主程序提供的 undefined 符号，
#   这些符号在 dlopen 时由主程序的动态符号表解析。
# ============================================================

# ── PlayPluginBackend：C++ 业务层 + QML 类型注册 ─────────────
qt_add_qml_module(PlayPluginBackend
    STATIC
    URI     PlayPlugin
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/PlayPlugin"

    SOURCES
        src/PlaybackContext.h src/PlaybackContext.cpp
        src/PlayerEngine.h    src/PlayerEngine.cpp
        src/PlaylistModel.h   src/PlaylistModel.cpp
        src/MediaInfo.h
)

target_include_directories(PlayPluginBackend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src     # PlayerEngine / PlaylistModel / MediaInfo
    ${CMAKE_SOURCE_DIR}/plugin          # IPlayerPlugin.h（含 PluginFinder 类型定义）
    ${CMAKE_SOURCE_DIR}/logger          # Logger.h
    ${CMAKE_SOURCE_DIR}/core            # PluginManager.h（头文件路径，供编译期类型检查）
)

target_link_libraries(PlayPluginBackend PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Multimedia
    VideoPlayerPlugin                   # IPlayerPlugin.h（INTERFACE）
    VideoPlayerLogger
    # 不再链接 VideoPlayerCore（STATIC）：
    # PluginManager 符号由主程序在运行时提供，无需编译期静态链接
)

# ── PlayPlugin：MODULE 库，QPluginLoader 运行时加载 ───────────
add_library(PlayPlugin MODULE
    PlayPlugin.h
    PlayPlugin.cpp
)

qt6_add_resources(PlayPlugin "playplugin_qml"
    PREFIX "/PlayPlugin"
    FILES
        qml/PlayPluginView.qml
        qml/PlayerView.qml
        qml/ControlBar.qml
        qml/PlaylistView.qml
)

set_target_properties(PlayPlugin PROPERTIES
    CXX_VISIBILITY_PRESET     hidden
    VISIBILITY_INLINES_HIDDEN ON
    LIBRARY_OUTPUT_DIRECTORY  "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY  "${CMAKE_BINARY_DIR}/plugins"
)

target_include_directories(PlayPlugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/plugin          # IPlayerPlugin.h
    ${CMAKE_SOURCE_DIR}/logger          # Logger.h
    ${CMAKE_SOURCE_DIR}/core            # PluginManager.h（仅头文件，符号运行时解析）
)

target_link_libraries(PlayPlugin PRIVATE
    PlayPluginBackend
    PlayPluginBackendplugin
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Multimedia
    VideoPlayerPlugin
    VideoPlayerLogger
    # 不链接 VideoPlayerCore（STATIC），改用运行时符号解析
)

# PluginManager 等宿主符号在 dlopen 后由主程序（-rdynamic）提供。
# --allow-shlib-undefined：链接 MODULE 时忽略来自主程序的 undefined 符号，
# 等价于 macOS 的 -undefined dynamic_lookup。
if(APPLE)
    target_link_options(PlayPlugin PRIVATE -Wl,-undefined,dynamic_lookup)
else()
    target_link_options(PlayPlugin PRIVATE -Wl,--allow-shlib-undefined)
endif()

# ── 安装规则 ────────────────────────────────────────────────
if(APPLE)
    install(TARGETS PlayPlugin
        LIBRARY DESTINATION "$<TARGET_BUNDLE_DIR:VideoPlayerApp>/Contents/PlugIns"
    )
    install(DIRECTORY "${CMAKE_BINARY_DIR}/PlayPlugin"
        DESTINATION "$<TARGET_BUNDLE_DIR:VideoPlayerApp>/Contents/Resources"
    )
else()
    install(TARGETS PlayPlugin
        LIBRARY DESTINATION plugins
        RUNTIME DESTINATION plugins
    )
    install(DIRECTORY "${CMAKE_BINARY_DIR}/PlayPlugin"
        DESTINATION "."
    )
endif()
