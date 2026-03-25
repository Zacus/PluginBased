# VideoPlayerCore —— 宿主级 STATIC 库
#
# 编译 PluginManager.cpp，对外暴露：
#   - PluginManager（QML_ELEMENT QML_SINGLETON，注册在宿主 VideoPlayer 1.0 模块）
#   - 头文件搜索路径（IPlayerPlugin.h / Logger.h）
#
# 同时被两个 target 链接：
#   - VideoPlayerApp（宿主可执行文件）
#   - PlayPlugin.so（插件 MODULE，initialize() 中调用 PluginManager::instance()）
#
# 注意：PluginManager 不再放进 qt_add_qml_module(VideoPlayerApp SOURCES ...)，
# 改由此 STATIC 库统一编译，避免符号只存在于可执行文件而插件链接不到的问题。
add_library(VideoPlayerCore STATIC
    PluginManager.h
    PluginManager.cpp
)

target_include_directories(VideoPlayerCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}         # PluginManager.h
    ${CMAKE_SOURCE_DIR}/plugin          # IPlayerPlugin.h
    ${CMAKE_SOURCE_DIR}/logger          # Logger.h
)

target_link_libraries(VideoPlayerCore PUBLIC
    Qt6::Core
    Qt6::Quick
    VideoPlayerLogger
    VideoPlayerPlugin
)
