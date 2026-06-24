cmake_minimum_required(VERSION 3.21)
project(PluginBased VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui GuiPrivate Quick QuickControls2 Multimedia ShaderTools Test LinguistTools)
qt_standard_project_setup()

find_package(spdlog REQUIRED)   # 可来自 vcpkg、conan 或系统安装

include(CTest)

# ── FFmpeg（通过 vcpkg + pkg-config）─────────────────────────────────────────
# 安装：vcpkg install ffmpeg[avcodec,avformat,avutil,swresample,swscale] pkgconf
# 或直接在 vcpkg.json 中声明依赖后 cmake 自动安装（manifest 模式）
#
# pkg_check_modules 的 IMPORTED_TARGET 参数会自动创建 PkgConfig::<NAME> target，
# 包含正确的 include 路径、库链接、编译选项，无需手动维护 FindFFmpeg.cmake。
find_package(PkgConfig REQUIRED)

pkg_check_modules(AVFORMAT   REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC    REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL     REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWSCALE    REQUIRED IMPORTED_TARGET libswscale)
pkg_check_modules(SWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)

if(APPLE)
    # macOS 上 pkgconf + 静态 FFmpeg 可能把裸 framework 名写入
    # INTERFACE_LINK_OPTIONS，导致 clang 把它们当作输入文件而不是
    # "-framework Foo" 参数。正确的 framework 依赖仍保留在导入库里，
    # 因此这里清空有问题的链接选项。
    foreach(_ffmpeg_target
            PkgConfig::AVFORMAT
            PkgConfig::AVCODEC
            PkgConfig::AVUTIL
            PkgConfig::SWSCALE
            PkgConfig::SWRESAMPLE)
        set_property(TARGET ${_ffmpeg_target} PROPERTY INTERFACE_LINK_OPTIONS "")
    endforeach()
endif()

# 创建聚合 INTERFACE target FFmpeg::all，业务代码只需链接这一个
# 和 spdlog::spdlog、Qt6::Core 的使用风格保持一致
add_library(FFmpeg::all INTERFACE IMPORTED)
target_link_libraries(FFmpeg::all INTERFACE
    PkgConfig::AVFORMAT
    PkgConfig::AVCODEC
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE
    PkgConfig::SWRESAMPLE
)

message(STATUS "FFmpeg avformat : ${AVFORMAT_VERSION}")
message(STATUS "FFmpeg avcodec  : ${AVCODEC_VERSION}")
message(STATUS "FFmpeg avutil   : ${AVUTIL_VERSION}")

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
add_subdirectory(sdk/media_core)
add_subdirectory(sdk/media_playback_runtime)

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
FetchContent_GetProperties(QtQuickComponents SOURCE_DIR PLUGINBASED_QTC_SOURCE_DIR)

set(PLUGINBASED_THEME_SOURCE_DIR "${PLUGINBASED_QTC_SOURCE_DIR}/themes")
set(PLUGINBASED_THEME_OUTPUT_DIR "${CMAKE_BINARY_DIR}/themes")
if(NOT IS_DIRECTORY "${PLUGINBASED_THEME_SOURCE_DIR}")
    message(FATAL_ERROR "QtQuickComponents theme directory not found: ${PLUGINBASED_THEME_SOURCE_DIR}")
endif()

file(MAKE_DIRECTORY "${PLUGINBASED_THEME_OUTPUT_DIR}")
add_custom_target(PluginBased_copy_themes ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGINBASED_THEME_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PLUGINBASED_THEME_SOURCE_DIR}"
            "${PLUGINBASED_THEME_OUTPUT_DIR}"
    COMMENT "Copying PluginBased runtime theme JSON files"
)
install(DIRECTORY "${PLUGINBASED_THEME_OUTPUT_DIR}/"
    DESTINATION "themes"
    FILES_MATCHING PATTERN "*.json"
)

add_subdirectory(tools/plugin_generator)

set(PLUGIN_MANIFEST "${CMAKE_SOURCE_DIR}/plugins.json")
if(NOT EXISTS "${PLUGIN_MANIFEST}")
    message(FATAL_ERROR "Plugin manifest not found: ${PLUGIN_MANIFEST}")
endif()
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")
configure_file(${PLUGIN_MANIFEST} "${CMAKE_BINARY_DIR}/plugins.json" COPYONLY)
install(FILES "${PLUGIN_MANIFEST}" DESTINATION ".")

file(READ "${PLUGIN_MANIFEST}" PLUGIN_MANIFEST_CONTENT)
string(JSON PLUGIN_COUNT ERROR_VARIABLE PLUGIN_JSON_ERROR
    LENGTH "${PLUGIN_MANIFEST_CONTENT}" plugins)
if(PLUGIN_JSON_ERROR)
    message(FATAL_ERROR "Invalid plugin manifest ${PLUGIN_MANIFEST}: ${PLUGIN_JSON_ERROR}")
endif()

if(PLUGIN_COUNT GREATER 0)
    math(EXPR PLUGIN_LAST_INDEX "${PLUGIN_COUNT} - 1")
    foreach(_plugin_index RANGE 0 ${PLUGIN_LAST_INDEX})
        string(JSON _plugin_name ERROR_VARIABLE PLUGIN_JSON_ERROR
            GET "${PLUGIN_MANIFEST_CONTENT}" plugins ${_plugin_index})
        if(PLUGIN_JSON_ERROR)
            message(FATAL_ERROR "Invalid plugin entry at index ${_plugin_index}: ${PLUGIN_JSON_ERROR}")
        endif()
        if(_plugin_name STREQUAL "" OR _plugin_name MATCHES "[/\\\\]" OR _plugin_name MATCHES "\\.\\.")
            message(FATAL_ERROR "Invalid plugin name in manifest: '${_plugin_name}'")
        endif()

        set(_plugin_dir "${CMAKE_SOURCE_DIR}/plugins/${_plugin_name}")
        if(NOT IS_DIRECTORY "${_plugin_dir}")
            message(FATAL_ERROR "Plugin directory listed in manifest does not exist: ${_plugin_dir}")
        endif()
        if(NOT EXISTS "${_plugin_dir}/CMakeLists.txt")
            message(FATAL_ERROR "Plugin listed in manifest has no CMakeLists.txt: ${_plugin_dir}")
        endif()
        if(NOT EXISTS "${_plugin_dir}/${_plugin_name}.json")
            message(FATAL_ERROR "Plugin listed in manifest has no metadata JSON: ${_plugin_dir}/${_plugin_name}.json")
        endif()

        add_subdirectory("${_plugin_dir}")
        configure_file("${_plugin_dir}/${_plugin_name}.json"
            "${CMAKE_BINARY_DIR}/plugins/${_plugin_name}.json"
            COPYONLY)
        if(APPLE)
            install(FILES "${_plugin_dir}/${_plugin_name}.json"
                DESTINATION "$<TARGET_BUNDLE_DIR:PluginBasedApp>/Contents/PlugIns")
        else()
            install(FILES "${_plugin_dir}/${_plugin_name}.json"
                DESTINATION plugins)
        endif()
    endforeach()
endif()

add_subdirectory(app)
add_dependencies(PluginBasedApp PluginBased_copy_themes)

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    add_test(NAME playplugin_regression_checks
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/playplugin_regression_checks.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    add_test(NAME playplugin_architecture_decoupling_checks
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/playplugin_architecture_decoupling_checks.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    add_test(NAME playplugin_surface_decoupling_checks
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/playplugin_surface_decoupling_checks.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    add_test(NAME playplugin_video_renderer_scheduler_checks
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/playplugin_video_renderer_scheduler_checks.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    add_test(NAME playplugin_qt_playback_adapter_checks
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/playplugin_qt_playback_adapter_checks.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    add_test(NAME playplugin_bplus_completion_checks
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/playplugin_bplus_completion_checks.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    add_test(NAME media_sdk_core_architecture_checks
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/media_sdk_core_architecture_checks.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    add_test(NAME media_sdk_playback_runtime_architecture_checks
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/media_sdk_playback_runtime_architecture_checks.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    add_test(NAME plugin_generator_checks
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/plugin_generator_checks.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    add_test(NAME host_plugin_generator_decoupling_checks
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/host_plugin_generator_decoupling_checks.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    add_test(NAME ci_ctest_checks
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/ci_ctest_checks.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    add_test(NAME i18n_architecture_checks
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/i18n_architecture_checks.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    add_executable(PluginMetadataValidatorTest
        tests/tst_plugin_metadata_validator.cpp
        core/PluginMetadataValidator.cpp
    )
    target_include_directories(PluginMetadataValidatorTest PRIVATE
        "${CMAKE_SOURCE_DIR}/core"
        "${CMAKE_SOURCE_DIR}/plugin"
    )
    target_link_libraries(PluginMetadataValidatorTest PRIVATE
        Qt6::Core
        Qt6::Test
        PluginBasedPlugin
    )
    add_test(NAME plugin_metadata_validator
        COMMAND $<TARGET_FILE:PluginMetadataValidatorTest>
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    add_executable(PluginDiscoveryTest
        tests/tst_plugin_discovery.cpp
        core/PluginDiscovery.cpp
    )
    target_include_directories(PluginDiscoveryTest PRIVATE
        "${CMAKE_SOURCE_DIR}/core"
    )
    target_link_libraries(PluginDiscoveryTest PRIVATE
        Qt6::Core
        Qt6::Test
    )
    add_test(NAME plugin_discovery
        COMMAND $<TARGET_FILE:PluginDiscoveryTest>
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    set(PLAYPLUGIN_TEST_PLUGIN_ROOT "${CMAKE_SOURCE_DIR}/plugins")
    add_executable(PlayPluginVideoFrameSchedulerTest
        tests/tst_playplugin_video_frame_scheduler.cpp
        "${PLAYPLUGIN_TEST_PLUGIN_ROOT}/PlayPlugin/src/video/VideoFrameScheduler.cpp"
    )
    target_include_directories(PlayPluginVideoFrameSchedulerTest PRIVATE
        "${PLAYPLUGIN_TEST_PLUGIN_ROOT}/PlayPlugin/src"
    )
    target_link_libraries(PlayPluginVideoFrameSchedulerTest PRIVATE
        Qt6::Core
        Qt6::Test
    )
    add_test(NAME playplugin_video_frame_scheduler
        COMMAND $<TARGET_FILE:PlayPluginVideoFrameSchedulerTest>
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    add_test(NAME app_theme_service_checks
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/app_theme_service_checks.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    add_test(NAME plugin_generator_backend_smoke
        COMMAND $<TARGET_FILE:PluginGeneratorBackendSmokeTest>
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()
