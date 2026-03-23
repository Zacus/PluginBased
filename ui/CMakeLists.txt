# ============================================================
# VideoPlayerUI — 可复用 QML 组件静态库
#
# 使用方式（在其他 target 的 CMakeLists.txt 里）：
#   target_link_libraries(YourTarget PRIVATE VideoPlayerUI)
#   然后在 QML 里: import VideoPlayer.UI 1.0
# ============================================================

qt_add_library(VideoPlayerUI STATIC)

qt_add_qml_module(VideoPlayerUI
    URI     "VideoPlayer.UI"
    VERSION 1.0
    # 纯 QML 组件库，无 C++ 类型注册，不需要运行时插件
    NO_PLUGIN
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/VideoPlayer/UI"
    QML_FILES
        IconButton.qml
        ProgressSlider.qml
)

target_link_libraries(VideoPlayerUI PUBLIC
    Qt6::Quick
    Qt6::QuickControls2
)
