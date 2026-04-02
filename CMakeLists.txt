cmake_minimum_required(VERSION 3.21)
project(VideoPlayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Multimedia)
qt_standard_project_setup()

find_package(spdlog REQUIRED)   # vcpkg / conan / system

if(WIN32)
    set(DUMP_LIBS dbghelp)
endif()

# # ── macOS RPATH 设置 ──────────────────────────────────────────
# # 让所有 SHARED/MODULE 库在构建时就带上正确的 install_name，
# # macdeployqt 才能解析 @rpath/Qt*.framework 并正确打包。
# #
# # CMAKE_MACOSX_RPATH ON：
# #   每个 dylib/so 的 install_name 使用 @rpath/<libname>（相对路径），
# #   而非绝对的构建目录路径。
# #
# # CMAKE_BUILD_RPATH：
# #   构建期 rpath，指向 Qt 安装目录内的 lib/，让开发期直接运行 .app 有效。
# #   从 Qt6_DIR 推导（Qt6_DIR 通常是 .../Qt/6.x.x/macos/lib/cmake/Qt6）。
# #
# # CMAKE_INSTALL_RPATH：
# #   install/打包后的 rpath，使用 @executable_path/../Frameworks，
# #   让 macdeployqt 把 Qt.framework 拷进 bundle 后能被找到。
# if(APPLE)
#     set(CMAKE_MACOSX_RPATH ON)

#     # 从 Qt6_DIR 推导 Qt lib 目录（开发期 rpath）
#     if(DEFINED Qt6_DIR)
#         get_filename_component(_qt_lib_dir "${Qt6_DIR}/../../.." ABSOLUTE)
#         set(CMAKE_BUILD_RPATH "${_qt_lib_dir}")
#     endif()

#     # 发布期 rpath：@executable_path/../Frameworks（macdeployqt 标准位置）
#     set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks")
#     set(CMAKE_INSTALL_RPATH_USE_LINK_PATH OFF)
# endif()

add_subdirectory(logger)
add_subdirectory(plugin)
add_subdirectory(core)
add_subdirectory(plugins/DummyPlugin)
add_subdirectory(plugins/PlayPlugin)

# ── UI 组件库：独立仓库，通过 FetchContent 引入 ───────────────
# 生产环境替换 SOURCE_DIR 为 GIT_REPOSITORY + GIT_TAG：
#   FetchContent_Declare(
#       QtQuickComponents
#       GIT_REPOSITORY https://github.com/yourname/QtQuickComponents.git
#       GIT_TAG        v1.0.0
#   )
# 告知组件库把 qmldir 和插件输出到本项目的构建目录，
# 确保运行时 QML 引擎能通过 QML_IMPORT_PATH 找到它们。
set(QTC_QML_OUTPUT_BASE "${CMAKE_BINARY_DIR}" CACHE PATH "" FORCE)
include(FetchContent)
FetchContent_Declare(
    QtQuickComponents
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/../QtQuickComponents"
)
FetchContent_MakeAvailable(QtQuickComponents)

add_subdirectory(app)
