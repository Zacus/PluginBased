# VideoPlayerCore —— INTERFACE 库（仅传递头文件搜索路径）
#
# PluginManager.cpp 由 qt_add_qml_module(VideoPlayerApp SOURCES ...) 编译进主程序。
# 主程序开启 ENABLE_EXPORTS（-rdynamic），PluginManager 符号进入动态符号表，
# 插件 .so 在 dlopen 后通过运行时动态链接自动解析，无需静态链接此库。
#
# 这是单向依赖的正确形态：
#   宿主 → 插件（加载、调用接口）
#   插件 → 宿主（运行时符号解析，无编译期静态依赖）
add_library(VideoPlayerCore INTERFACE)

target_include_directories(VideoPlayerCore INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}         # PluginManager.h（插件需要 #include 以使用类型）
    ${CMAKE_SOURCE_DIR}/plugin          # IAppPlugin.h / IPlayerPlugin.h
    ${CMAKE_SOURCE_DIR}/logger          # Logger.h
)

target_link_libraries(VideoPlayerCore INTERFACE
    Qt6::Core
    Qt6::Quick
    VideoPlayerLogger
    VideoPlayerPlugin
)
