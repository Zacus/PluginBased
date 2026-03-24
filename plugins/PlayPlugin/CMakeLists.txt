# ============================================================
# PlayPlugin — 内置播放器界面插件
#
# 将播放页面（PlayerView / ControlBar / PlaylistView）封装为
# 标准 IPlayerPlugin。宿主通过 qmlComponentUrl() 加载 QML 界面。
#
# 使用 MODULE 而非 SHARED，专为 QPluginLoader 运行时加载设计。
# ============================================================
add_library(PlayPlugin MODULE
    PlayPlugin.h
    PlayPlugin.cpp
)

# ── QML 资源打包进插件动态库 ─────────────────────────────────
# qt_add_resources 直接作用于 MODULE target，将 QML 文件以 qrc 形式
# 嵌入插件 .so 自身，前缀 /PlayPlugin。
# 运行时路径：qrc:/PlayPlugin/qml/PlayPluginView.qml
qt6_add_resources(PlayPlugin "playplugin_qml"
    PREFIX  "/PlayPlugin"
    FILES
        qml/PlayPluginView.qml
)

# ── 符号可见性 ───────────────────────────────────────────────
set_target_properties(PlayPlugin PROPERTIES
    CXX_VISIBILITY_PRESET     hidden
    VISIBILITY_INLINES_HIDDEN ON
    LIBRARY_OUTPUT_DIRECTORY  "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY  "${CMAKE_BINARY_DIR}/plugins"
)

target_include_directories(PlayPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/plugin
    ${CMAKE_SOURCE_DIR}/logger
    ${CMAKE_SOURCE_DIR}/core
)

target_link_libraries(PlayPlugin PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    VideoPlayerPlugin   # IPlayerPlugin 接口头文件
    VideoPlayerLogger
)

# ── 安装规则 ────────────────────────────────────────────────
if(APPLE)
    install(TARGETS PlayPlugin
        LIBRARY DESTINATION "$<TARGET_BUNDLE_DIR:VideoPlayerApp>/Contents/PlugIns"
    )
else()
    install(TARGETS PlayPlugin
        LIBRARY DESTINATION plugins
        RUNTIME DESTINATION plugins
    )
endif()

# ── 符号可见性 ───────────────────────────────────────────────
set_target_properties(PlayPlugin PROPERTIES
    CXX_VISIBILITY_PRESET     hidden
    VISIBILITY_INLINES_HIDDEN ON
    LIBRARY_OUTPUT_DIRECTORY  "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY  "${CMAKE_BINARY_DIR}/plugins"
)

target_include_directories(PlayPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/plugin
    ${CMAKE_SOURCE_DIR}/logger
    ${CMAKE_SOURCE_DIR}/core
)

target_link_libraries(PlayPlugin PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    VideoPlayerPlugin   # IPlayerPlugin 接口头文件
    VideoPlayerLogger
)

# ── 安装规则 ────────────────────────────────────────────────
if(APPLE)
    install(TARGETS PlayPlugin
        LIBRARY DESTINATION "$<TARGET_BUNDLE_DIR:VideoPlayerApp>/Contents/PlugIns"
    )
else()
    install(TARGETS PlayPlugin
        LIBRARY DESTINATION plugins
        RUNTIME DESTINATION plugins
    )
endif()