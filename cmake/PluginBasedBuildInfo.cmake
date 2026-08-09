include_guard(GLOBAL)

function(pluginbased_attach_build_info target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "Build-info target does not exist: ${target}")
    endif()

    if(APPLE)
        set(_platform "macOS")
    elseif(WIN32)
        set(_platform "Windows")
    elseif(UNIX)
        set(_platform "Linux")
    else()
        set(_platform "${CMAKE_SYSTEM_NAME}")
    endif()

    set(_generated_dir "${CMAKE_BINARY_DIR}/generated")
    set(_header "${_generated_dir}/BuildInfoData.h")
    set(_json "${CMAKE_BINARY_DIR}/build-info.json")
    set(_generator "${CMAKE_SOURCE_DIR}/cmake/GenerateBuildInfo.cmake")
    set(_generator_args
        "-DPLUGINBASED_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "-DPLUGINBASED_OUTPUT_HEADER=${_header}"
        "-DPLUGINBASED_OUTPUT_JSON=${_json}"
        "-DPLUGINBASED_PRODUCT_NAME=${PROJECT_NAME}"
        "-DPLUGINBASED_PRODUCT_VERSION=${PROJECT_VERSION}"
        "-DPLUGINBASED_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "-DPLUGINBASED_PLATFORM=${_platform}"
        "-DPLUGINBASED_ARCHITECTURE=${CMAKE_SYSTEM_PROCESSOR}"
        "-DPLUGINBASED_COMPILER=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
        "-DPLUGINBASED_QT_VERSION=${Qt6_VERSION}"
        "-DPLUGINBASED_OFFICIAL_BUILD=${PLUGINBASED_OFFICIAL_BUILD}"
        "-DPLUGINBASED_EXPECTED_TAG=${PLUGINBASED_EXPECTED_TAG}"
    )

    execute_process(
        COMMAND "${CMAKE_COMMAND}" ${_generator_args} -P "${_generator}"
        COMMAND_ERROR_IS_FATAL ANY
    )

    set(_refresh_target "${target}_build_info_refresh")
    add_custom_target("${_refresh_target}" ALL
        COMMAND "${CMAKE_COMMAND}" ${_generator_args} -P "${_generator}"
        BYPRODUCTS "${_header}" "${_json}"
        COMMENT "Refreshing PluginBased build identity"
        VERBATIM
    )
    add_dependencies("${target}" "${_refresh_target}")
    target_include_directories("${target}" PRIVATE "${_generated_dir}")

    set(PLUGINBASED_BUILD_INFO_HEADER "${_header}" PARENT_SCOPE)
    set(PLUGINBASED_BUILD_INFO_JSON "${_json}" PARENT_SCOPE)
endfunction()
