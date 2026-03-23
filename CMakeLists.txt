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
add_subdirectory(app)
