# ── PluginBasedLogger：日志 STATIC 库 + AppLog QML 模块 ──────────
#
# 产物：
#   libPluginBasedLogger.a   C++ 日志库（spdlog 封装，LOG_INFO 等宏）
#   AppLog/qmldir            QML 模块目录（URI="AppLog" Version=1.0）
#
# QML 用法（所有插件和宿主均可使用）：
#   import AppLog 1.0
#   Log.info("[MyPlugin] ready")
#
# 设计说明：
#   Logger.h/.cpp 是纯 C++ 文件，不含 QML_ELEMENT，
#   通过 target_sources 加入 target 而非放进 qt_add_qml_module SOURCES，
#   避免 qmltyperegistrar 扫描无关文件。
#   QmlLogger.h/.cpp 含 QML_ELEMENT / QML_SINGLETON，放进 qt_add_qml_module SOURCES。

qt_add_qml_module(PluginBasedLogger
    URI     AppLog
    VERSION 1.0
    RESOURCE_PREFIX "/"
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/AppLog"
    SOURCES
        QmlLogger.h
        QmlLogger.cpp
)

# 纯 C++ 实现文件，不走 qmltyperegistrar 扫描
target_sources(PluginBasedLogger PRIVATE
    Logger.h
    Logger.cpp
)

target_include_directories(PluginBasedLogger PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(PluginBasedLogger PUBLIC
    Qt6::Core
    Qt6::Quick
    spdlog::spdlog
)
