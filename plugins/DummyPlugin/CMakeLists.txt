# ============================================================
# DummyPlugin — 动态插件示例
#
# 使用 MODULE 而非 SHARED：
#   - MODULE 专为运行时 dlopen/QPluginLoader 加载设计
#   - 不能被其他 target 链接（link_libraries 会报错），强制正确用法
#   - macOS 上后缀为 .so，Windows 上为 .dll
# ============================================================
add_library(DummyPlugin MODULE
    DummyPlugin.h
    DummyPlugin.cpp
)

qt_add_qml_module(DummyPlugin
    NO_PLUGIN
    URI     DummyPlugin
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/DummyPlugin"
    QML_FILES
        qml/DummyPluginView.qml
)
# 符号可见性：只导出 Qt 插件入口符号，减小库体积、避免符号污染
set_target_properties(DummyPlugin PROPERTIES
    CXX_VISIBILITY_PRESET     hidden
    VISIBILITY_INLINES_HIDDEN ON
    # 统一输出到构建目录下的 plugins/ 子目录
    LIBRARY_OUTPUT_DIRECTORY  "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY  "${CMAKE_BINARY_DIR}/plugins"
)

target_include_directories(DummyPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/plugin
    ${CMAKE_SOURCE_DIR}/logger
)

target_link_libraries(DummyPlugin PRIVATE
    Qt6::Core
    VideoPlayerPlugin
    VideoPlayerLogger
    QtQuickComponents
)

# ── 安装规则（make install / CPack 打包时使用）────────────────
# 发布包结构：
#   bin/VideoPlayerApp
#   plugins/DummyPlugin.so   (Linux)
#   plugins/DummyPlugin.dll  (Windows)
# macOS .app bundle 结构：
#   VideoPlayerApp.app/Contents/MacOS/VideoPlayerApp
#   VideoPlayerApp.app/Contents/PlugIns/DummyPlugin.so
if(APPLE)
    install(TARGETS DummyPlugin
        LIBRARY DESTINATION "$<TARGET_BUNDLE_DIR:VideoPlayerApp>/Contents/PlugIns"
    )
else()
    install(TARGETS DummyPlugin
        LIBRARY DESTINATION plugins
        RUNTIME DESTINATION plugins
    )
endif()
