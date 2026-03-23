cmake_minimum_required(VERSION 3.21)
project(VideoPlayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ── Qt ──────────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Multimedia)
qt_standard_project_setup()

# ── spdlog（FetchContent，无需提前安装）─────────────────────────────────────
find_package(spdlog REQUIRED)   # vcpkg / conan / system
# include(FetchContent)
# FetchContent_Declare(
#     spdlog
#     GIT_REPOSITORY https://github.com/gabime/spdlog.git
#     GIT_TAG        v1.13.0
# )
# set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# set(SPDLOG_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
# set(SPDLOG_INSTALL        OFF CACHE BOOL "" FORCE)
# FetchContent_MakeAvailable(spdlog)

# ── Platform dump libs ──────────────────────────────────────────────────────
if(WIN32)
    set(DUMP_LIBS dbghelp)
endif()

# ── 子模块（logger 最先，其他模块都依赖它）──────────────────────────────────
add_subdirectory(logger)
add_subdirectory(plugin)
add_subdirectory(core)
add_subdirectory(plugins/DummyPlugin)
add_subdirectory(app)
