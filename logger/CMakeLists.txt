# ── VideoPlayerLogger：日志 STATIC 库 + AppLog QML 模块 ──────
#
# 编译产物：
#   libVideoPlayerLogger.a  — C++ 日志库（LOG_INFO 等宏）
#   AppLog/qmldir           — QML 模块目录，供 addImportPath 发现
#
# QML 用法：
#   import AppLog 1.0
#   Log.info("message")   // Log 是 QmlLogger 的 QML_NAMED_ELEMENT 名
#
# 宿主和所有插件都链接 VideoPlayerLogger，
# 因此 AppLog 模块对整个进程只有一份 Logger 实例，日志不分裂。

qt_add_qml_module(VideoPlayerLogger
    URI     AppLog
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/AppLog"

    SOURCES
        Logger.h
        Logger.cpp
        QmlLogger.h
        QmlLogger.cpp
)

target_include_directories(VideoPlayerLogger PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(VideoPlayerLogger PUBLIC
    Qt6::Core
    Qt6::Quick
    spdlog::spdlog
)
