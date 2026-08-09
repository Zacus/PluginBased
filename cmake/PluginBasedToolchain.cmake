include_guard(GLOBAL)

if(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
    set(_pluginbased_home "$ENV{HOME}")
elseif(DEFINED ENV{USERPROFILE} AND NOT "$ENV{USERPROFILE}" STREQUAL "")
    set(_pluginbased_home "$ENV{USERPROFILE}")
else()
    message(FATAL_ERROR
        "Cannot resolve the user home directory. Set VCPKG_ROOT and QT_ROOT explicitly.")
endif()

if(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
    set(_pluginbased_vcpkg_root "$ENV{VCPKG_ROOT}")
else()
    set(_pluginbased_vcpkg_root "${_pluginbased_home}/vcpkg/vcpkg-master")
endif()

if(DEFINED ENV{QT_ROOT} AND NOT "$ENV{QT_ROOT}" STREQUAL "")
    set(_pluginbased_qt_root "$ENV{QT_ROOT}")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(_pluginbased_qt_root "${_pluginbased_home}/Qt/6.8.3/macos")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(_pluginbased_qt_root "${_pluginbased_home}/Qt/6.8.3/gcc_64")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(_pluginbased_qt_root "${_pluginbased_home}/Qt/6.8.3/msvc2022_64")
else()
    message(FATAL_ERROR
        "Unsupported host '${CMAKE_HOST_SYSTEM_NAME}'. Set QT_ROOT explicitly.")
endif()

file(TO_CMAKE_PATH "${_pluginbased_vcpkg_root}" _pluginbased_vcpkg_root)
file(TO_CMAKE_PATH "${_pluginbased_qt_root}" _pluginbased_qt_root)
set(_pluginbased_vcpkg_toolchain
    "${_pluginbased_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")
set(_pluginbased_qt_config
    "${_pluginbased_qt_root}/lib/cmake/Qt6/Qt6Config.cmake")

if(NOT EXISTS "${_pluginbased_vcpkg_toolchain}")
    message(FATAL_ERROR
        "vcpkg toolchain not found: ${_pluginbased_vcpkg_toolchain}\n"
        "Run: python3 tools/setup_build_environment.py --configure debug")
endif()

if(NOT EXISTS "${_pluginbased_qt_config}")
    message(FATAL_ERROR
        "Qt 6.8.3 configuration not found: ${_pluginbased_qt_config}\n"
        "Run: python3 tools/setup_build_environment.py --configure debug")
endif()

set(PLUGINBASED_VCPKG_ROOT "${_pluginbased_vcpkg_root}"
    CACHE PATH "Resolved PluginBased vcpkg root" FORCE)
set(PLUGINBASED_QT_ROOT "${_pluginbased_qt_root}"
    CACHE PATH "Resolved PluginBased Qt root" FORCE)
set(Qt6_DIR "${_pluginbased_qt_root}/lib/cmake/Qt6"
    CACHE PATH "Qt 6 package configuration directory" FORCE)
list(PREPEND CMAKE_PREFIX_PATH "${_pluginbased_qt_root}")
list(REMOVE_DUPLICATES CMAKE_PREFIX_PATH)
set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}"
    CACHE STRING "Package search prefixes" FORCE)

include("${_pluginbased_vcpkg_toolchain}")
