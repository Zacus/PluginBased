# ============================================================
# PlayPlugin —— 完全自包含的播放器插件
#
# 「双 target」架构：
#
#  ┌─────────────────────────────────────────────────────────┐
#  │ PlayPluginBackend  (STATIC)                             │
#  │   qt_add_qml_module → URI="PlayPlugin" Version=1.0     │
#  │   C++ 类型：PlayerEngine / PlaylistModel / MediaInfo    │
#  └──────────────────────────┬──────────────────────────────┘
#                             │ 链接
#  ┌──────────────────────────▼──────────────────────────────┐
#  │ PlayPlugin  (MODULE — QPluginLoader 运行时加载)         │
#  │   Q_PLUGIN_METADATA IID=IPlayerPlugin_IID               │
#  │   qt6_add_resources 内嵌全部 QML 文件                   │
#  │   链接 VideoPlayerCore (STATIC) 获得 PluginManager 符号 │
#  └─────────────────────────────────────────────────────────┘
# ============================================================

# ── PlayPluginBackend：C++ 业务层 + QML 类型注册 ─────────────
qt_add_qml_module(PlayPluginBackend
    STATIC
    URI     PlayPlugin
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/PlayPlugin"

    SOURCES
        src/PlayerEngine.h    src/PlayerEngine.cpp
        src/PlaylistModel.h   src/PlaylistModel.cpp
        src/MediaInfo.h
)

target_include_directories(PlayPluginBackend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src     # PlayerEngine / PlaylistModel / MediaInfo
    ${CMAKE_SOURCE_DIR}/plugin          # IPlayerPlugin.h
    ${CMAKE_SOURCE_DIR}/logger          # Logger.h
    ${CMAKE_SOURCE_DIR}/core            # PluginManager.h（PlayerEngine 注册表用到）
)

target_link_libraries(PlayPluginBackend PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Multimedia
    VideoPlayerCore                     # PluginManager 符号 + 头文件（STATIC，传递依赖）
    VideoPlayerPlugin                   # IPlayerPlugin.h（INTERFACE）
    # VideoPlayerLogger 已由 VideoPlayerCore PUBLIC 传递，无需重复列出
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
    ${CMAKE_SOURCE_DIR}/plugin
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/logger
)

target_link_libraries(PlayPlugin PRIVATE
    PlayPluginBackend           # 业务类型（PlayerEngine 等）+ QML Backend 静态库
    PlayPluginBackendplugin     # qt_add_qml_module 生成的 QML runtime 注册插件
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Multimedia
    VideoPlayerCore             # PluginManager::instance().findPlugin()（STATIC，含符号）
    VideoPlayerPlugin           # IPlayerPlugin.h
    # VideoPlayerLogger 已由 VideoPlayerCore 传递
)

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
