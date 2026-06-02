# ============================================================
# PlayPlugin —— 完全自包含的播放器插件（单 target）
#
# Step 3：将原来的双 target（PlayPluginBackend + PlayPlugin）
# 合并为单一 MODULE target。
#
# 为什么原来需要双 target：
#   Qt6 的 qt_add_qml_module 默认会额外生成一个 QML plugin 入口文件，
#   其中包含 qt_plugin_instance / qt_plugin_query_metadata_v2，
#   与 PlayPlugin.cpp 里 Q_PLUGIN_METADATA(IID IPlayerPlugin_IID) 产生的同名符号冲突，
#   不得不拆成 PlayPluginBackend（QML 类型注册）+ PlayPlugin（IPlayerPlugin 实现）。
#
# 单 target 可行的关键：NO_PLUGIN 参数
#   NO_PLUGIN 告知 qt_add_qml_module 不生成 QML plugin 入口文件，
#   PlayPlugin.so 里只有 IPlayerPlugin 那一个 qt_plugin_instance，冲突消除。
#   QML 类型注册代码（Q_CONSTRUCTOR_FUNCTION）仍然生成并编译进 .so，
#   在 QPluginLoader dlopen 时自动触发，QML 引擎 import PlayPlugin 1.0 正常工作。
#
# 最终 PlayPlugin.so 包含：
#   ├─ PlayPlugin.cpp         IPlayerPlugin 实现（Q_PLUGIN_METADATA IPlayerPlugin_IID）
#   ├─ PlayerEngine.cpp       播放引擎（QML_ELEMENT）
#   ├─ PlaylistModel.cpp      播放列表模型（QML_ELEMENT）
#   ├─ PlaybackContext.cpp    模块上下文单例（QML_SINGLETON）
#   ├─ MediaInfo.h            媒体信息（QML_ELEMENT QML_UNCREATABLE）
#   └─ qml/*                  所有 QML 文件（嵌入 qrc，qt_add_qml_module 自动处理）
# ============================================================

# ── Step 1：先声明 MODULE target ──────────────────────────────
# MODULE 必须先于 qt_add_qml_module 创建，
# 否则后者会尝试自己创建一个 SHARED 库。
add_library(PlayPlugin MODULE
    PlayPlugin.h
    PlayPlugin.cpp
)

# ── Step 2：追加 QML 模块到该 MODULE target ───────────────────
# NO_PLUGIN：不生成独立的 QML plugin 入口文件（*Plugin.cpp）。
#
# 为什么必须用 NO_PLUGIN：
#   PlayPlugin.cpp 里有 Q_PLUGIN_METADATA(IID IPlayerPlugin_IID)，
#   AUTOMOC 会为它生成 qt_plugin_instance / qt_plugin_query_metadata_v2。
#   若同时指定 PLUGIN_TARGET PlayPlugin，qt_add_qml_module 还会生成
#   PlayPlugin_PlayPluginPlugin.cpp，其中包含同名的 QML plugin 入口符号，
#   导致链接时 duplicate symbol 报错。
#   NO_PLUGIN 告知 qt_add_qml_module 跳过生成这个文件，
#   PlayPlugin.so 里只保留 IPlayerPlugin 那一个 qt_plugin_instance。
#
# QML 类型仍然正常注册：
#   qmltyperegistrar 生成的注册代码里含 Q_CONSTRUCTOR_FUNCTION，
#   PlayPlugin.so 被 QPluginLoader dlopen 时自动执行，无需 qt_plugin_instance 触发。
#   前提（已满足）：QPluginLoader 加载在 QML 引擎 import 之前（main.cpp initPlugins）。
qt_add_qml_module(PlayPlugin
    NO_PLUGIN
    URI     PlayPlugin
    VERSION 1.0
    # QML module 目录（含 qmldir），宿主 addImportPath(CMAKE_BINARY_DIR) 即可找到
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/PlayPlugin"

    # QML 文件由 qt_add_qml_module 自动嵌入 qrc，前缀 /PlayPlugin/
    # 运行时路径：qrc:/PlayPlugin/qml/PlayPluginView.qml
    # 与 PlayPlugin::qmlComponentUrl() 返回值一致，无需手动 qt6_add_resources
    QML_FILES
        qml/PlayPluginView.qml
        qml/PlayerView.qml
        qml/ControlBar.qml
        qml/PlaylistView.qml

    # C++ 源文件（含 QML_ELEMENT / QML_SINGLETON 标注的类）
    SOURCES
        src/PlaybackContext.h  src/PlaybackContext.cpp
        src/PlayerEngine.h     src/PlayerEngine.cpp
        src/PlaylistModel.h    src/PlaylistModel.cpp
        src/MediaInfo.h
        # Step 2 — FFmpeg 解码器
        src/FFmpegUtils.h
        src/FrameQueue.h
        src/FFmpegDecoder.h    src/FFmpegDecoder.cpp
        src/hw/HardwareDecoderBackend.h
        src/hw/HardwareDecoderFactory.h src/hw/HardwareDecoderFactory.cpp
        src/hw/VideoToolboxBackend.h    src/hw/VideoToolboxBackend.cpp
        src/hw/D3D11VABackend.h         src/hw/D3D11VABackend.cpp
        src/hw/VaapiBackend.h           src/hw/VaapiBackend.cpp
        # Step 3 — 渲染器 + 时钟同步 + 视频输出组件
        src/ClockSync.h
        src/AudioRenderer.h    src/AudioRenderer.cpp
        src/VideoRenderer.h    src/VideoRenderer.cpp
        src/FFmpegSurface.h    src/FFmpegSurface.cpp
)

qt6_add_shaders(PlayPlugin "PlayPluginShaders"
    PREFIX "/PlayPlugin/shaders"
    BASE shaders
    BATCHABLE
    FILES
        shaders/yuvvideo.vert
        shaders/yuvvideo.frag
)

# ── 编译属性 ──────────────────────────────────────────────────
set_target_properties(PlayPlugin PROPERTIES
    CXX_VISIBILITY_PRESET     hidden
    VISIBILITY_INLINES_HIDDEN ON
    # .so 输出到 plugins/，供 QPluginLoader 扫描
    LIBRARY_OUTPUT_DIRECTORY  "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY  "${CMAKE_BINARY_DIR}/plugins"
    # QML module 的 qmldir 输出到 CMAKE_BINARY_DIR/PlayPlugin/（见 OUTPUT_DIRECTORY）
)

target_include_directories(PlayPlugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src     # PlaybackContext / PlayerEngine / PlaylistModel / MediaInfo
    ${CMAKE_SOURCE_DIR}/plugin          # IPlayerPlugin.h
    ${CMAKE_SOURCE_DIR}/logger          # Logger.h
    ${CMAKE_SOURCE_DIR}/core            # PluginManager.h（头文件，符号运行时解析）
)

target_link_libraries(PlayPlugin PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::GuiPrivate
    Qt6::Quick
    Qt6::Multimedia
    VideoPlayerPlugin                   # IPlayerPlugin.h（INTERFACE，零编译产物）
    VideoPlayerLogger                   # spdlog 封装（STATIC）
    FFmpeg::all                         # avformat / avcodec / avutil / swscale / swresample
    # 不链接 VideoPlayerCore：PluginManager 符号由主程序 -rdynamic 在运行时提供
)

# ── 运行时符号解析选项 ────────────────────────────────────────
# PluginManager 等宿主符号在 dlopen 后由主程序（ENABLE_EXPORTS=ON）提供。
if(APPLE)
    target_link_options(PlayPlugin PRIVATE -Wl,-undefined,dynamic_lookup)
else()
    target_link_options(PlayPlugin PRIVATE -Wl,--allow-shlib-undefined)
endif()

# ── 安装规则 ─────────────────────────────────────────────────
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
    # 部署 QML 模块目录（宿主 addImportPath 指向其父目录 CMAKE_BINARY_DIR）
    install(DIRECTORY "${CMAKE_BINARY_DIR}/PlayPlugin"
        DESTINATION "."
    )
endif()
