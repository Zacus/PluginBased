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
