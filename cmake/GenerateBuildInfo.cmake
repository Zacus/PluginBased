cmake_minimum_required(VERSION 3.24)

foreach(_required IN ITEMS
        PLUGINBASED_SOURCE_DIR
        PLUGINBASED_OUTPUT_HEADER
        PLUGINBASED_OUTPUT_JSON
        PLUGINBASED_PRODUCT_NAME
        PLUGINBASED_PRODUCT_VERSION
        PLUGINBASED_BUILD_TYPE
        PLUGINBASED_PLATFORM
        PLUGINBASED_ARCHITECTURE
        PLUGINBASED_COMPILER
        PLUGINBASED_QT_VERSION)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "Missing required build-info input: ${_required}")
    endif()
endforeach()

if(NOT PLUGINBASED_PRODUCT_VERSION MATCHES "^[0-9]+\.[0-9]+\.[0-9]+$")
    message(FATAL_ERROR
        "Product version must match MAJOR.MINOR.PATCH: ${PLUGINBASED_PRODUCT_VERSION}")
endif()

if(NOT DEFINED PLUGINBASED_OFFICIAL_BUILD)
    set(PLUGINBASED_OFFICIAL_BUILD OFF)
endif()
if(NOT DEFINED PLUGINBASED_EXPECTED_TAG)
    set(PLUGINBASED_EXPECTED_TAG "")
endif()

function(pluginbased_escape_string input output)
    set(_value "${input}")
    string(REPLACE "\\" "\\\\" _value "${_value}")
    string(REPLACE "\"" "\\\"" _value "${_value}")
    string(REPLACE "\r" "\\r" _value "${_value}")
    string(REPLACE "\n" "\\n" _value "${_value}")
    string(REPLACE "\t" "\\t" _value "${_value}")
    set(${output} "${_value}" PARENT_SCOPE)
endfunction()

function(pluginbased_write_if_different output_path content)
    get_filename_component(_output_directory "${output_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${_output_directory}")
    set(_temporary_path "${output_path}.tmp")
    file(WRITE "${_temporary_path}" "${content}")

    set(_different TRUE)
    if(EXISTS "${output_path}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E compare_files
                    "${_temporary_path}" "${output_path}"
            RESULT_VARIABLE _compare_result
        )
        if(_compare_result EQUAL 0)
            set(_different FALSE)
        endif()
    endif()

    if(_different)
        file(RENAME "${_temporary_path}" "${output_path}" RESULT _rename_result)
        if(NOT _rename_result STREQUAL "0")
            message(FATAL_ERROR
                "Failed to replace build-info output ${output_path}: ${_rename_result}")
        endif()
    else()
        file(REMOVE "${_temporary_path}")
    endif()
endfunction()

set(_git_commit "")
set(_git_short "unknown")
set(_git_tag "")
set(_tree_state "unknown")

find_program(GIT_EXECUTABLE NAMES git)
if(GIT_EXECUTABLE AND EXISTS "${PLUGINBASED_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${PLUGINBASED_SOURCE_DIR}" rev-parse HEAD
        RESULT_VARIABLE _git_result
        OUTPUT_VARIABLE _git_commit
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    string(LENGTH "${_git_commit}" _git_commit_length)
    if(_git_result EQUAL 0
            AND _git_commit MATCHES "^[0-9a-fA-F]+$"
            AND (_git_commit_length EQUAL 40 OR _git_commit_length EQUAL 64))
        string(TOLOWER "${_git_commit}" _git_commit)
        string(SUBSTRING "${_git_commit}" 0 8 _git_short)

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${PLUGINBASED_SOURCE_DIR}"
                    status --porcelain --untracked-files=normal
            RESULT_VARIABLE _status_result
            OUTPUT_VARIABLE _git_status
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(NOT _status_result EQUAL 0)
            set(_tree_state "unknown")
        elseif(_git_status STREQUAL "")
            set(_tree_state "clean")
        else()
            set(_tree_state "dirty")
        endif()

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${PLUGINBASED_SOURCE_DIR}"
                    describe --tags --exact-match --match "v[0-9]*" HEAD
            RESULT_VARIABLE _tag_result
            OUTPUT_VARIABLE _git_tag
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(NOT _tag_result EQUAL 0)
            set(_git_tag "")
        endif()
    else()
        set(_git_commit "")
    endif()
endif()

set(_product_tag "v${PLUGINBASED_PRODUCT_VERSION}")
if(PLUGINBASED_OFFICIAL_BUILD)
    if(NOT PLUGINBASED_EXPECTED_TAG MATCHES "^v[0-9]+\.[0-9]+\.[0-9]+$")
        message(FATAL_ERROR
            "Official build tag must match vMAJOR.MINOR.PATCH: ${PLUGINBASED_EXPECTED_TAG}")
    endif()
    if(NOT PLUGINBASED_EXPECTED_TAG STREQUAL _product_tag)
        message(FATAL_ERROR
            "Official build tag ${PLUGINBASED_EXPECTED_TAG} does not match product version ${PLUGINBASED_PRODUCT_VERSION}")
    endif()
    if(_git_commit STREQUAL "")
        message(FATAL_ERROR "Official build requires a known Git commit")
    endif()
    if(NOT _tree_state STREQUAL "clean")
        message(FATAL_ERROR
            "Official build requires a clean Git tree; current state is ${_tree_state}")
    endif()
    if(NOT _git_tag STREQUAL PLUGINBASED_EXPECTED_TAG)
        message(FATAL_ERROR
            "Git tag at HEAD is '${_git_tag}', expected '${PLUGINBASED_EXPECTED_TAG}'")
    endif()
endif()

if(_git_commit STREQUAL "")
    set(_display_version "${PLUGINBASED_PRODUCT_VERSION}+unknown")
    message(STATUS "Build identity: Git metadata unavailable; using unknown identity")
elseif(_tree_state STREQUAL "clean" AND _git_tag STREQUAL _product_tag)
    set(_display_version "${PLUGINBASED_PRODUCT_VERSION}")
elseif(_tree_state STREQUAL "dirty")
    set(_display_version "${PLUGINBASED_PRODUCT_VERSION}+g${_git_short}.dirty")
else()
    set(_display_version "${PLUGINBASED_PRODUCT_VERSION}+g${_git_short}")
endif()

foreach(_field IN ITEMS
        PLUGINBASED_PRODUCT_NAME
        PLUGINBASED_PRODUCT_VERSION
        PLUGINBASED_BUILD_TYPE
        PLUGINBASED_PLATFORM
        PLUGINBASED_ARCHITECTURE
        PLUGINBASED_COMPILER
        PLUGINBASED_QT_VERSION
        _display_version
        _git_commit
        _git_short
        _git_tag
        _tree_state)
    pluginbased_escape_string("${${_field}}" "${_field}_escaped")
endforeach()

set(_header_content "#pragma once\n\nnamespace PluginBased::BuildInfoData {\n\n")
string(APPEND _header_content "inline constexpr int SchemaVersion = 1;\n")
string(APPEND _header_content "inline constexpr char ProductName[] = \"${PLUGINBASED_PRODUCT_NAME_escaped}\";\n")
string(APPEND _header_content "inline constexpr char ProductVersion[] = \"${PLUGINBASED_PRODUCT_VERSION_escaped}\";\n")
string(APPEND _header_content "inline constexpr char DisplayVersion[] = \"${_display_version_escaped}\";\n")
string(APPEND _header_content "inline constexpr char GitCommit[] = \"${_git_commit_escaped}\";\n")
string(APPEND _header_content "inline constexpr char GitShortCommit[] = \"${_git_short_escaped}\";\n")
string(APPEND _header_content "inline constexpr char GitTag[] = \"${_git_tag_escaped}\";\n")
string(APPEND _header_content "inline constexpr char GitTreeState[] = \"${_tree_state_escaped}\";\n")
string(APPEND _header_content "inline constexpr char BuildType[] = \"${PLUGINBASED_BUILD_TYPE_escaped}\";\n")
string(APPEND _header_content "inline constexpr char Platform[] = \"${PLUGINBASED_PLATFORM_escaped}\";\n")
string(APPEND _header_content "inline constexpr char Architecture[] = \"${PLUGINBASED_ARCHITECTURE_escaped}\";\n")
string(APPEND _header_content "inline constexpr char Compiler[] = \"${PLUGINBASED_COMPILER_escaped}\";\n")
string(APPEND _header_content "inline constexpr char QtVersion[] = \"${PLUGINBASED_QT_VERSION_escaped}\";\n\n")
string(APPEND _header_content "} // namespace PluginBased::BuildInfoData\n")

set(_json_content "{\n")
string(APPEND _json_content "  \"schemaVersion\": 1,\n")
string(APPEND _json_content "  \"productName\": \"${PLUGINBASED_PRODUCT_NAME_escaped}\",\n")
string(APPEND _json_content "  \"productVersion\": \"${PLUGINBASED_PRODUCT_VERSION_escaped}\",\n")
string(APPEND _json_content "  \"displayVersion\": \"${_display_version_escaped}\",\n")
string(APPEND _json_content "  \"gitCommit\": \"${_git_commit_escaped}\",\n")
string(APPEND _json_content "  \"gitShortCommit\": \"${_git_short_escaped}\",\n")
string(APPEND _json_content "  \"gitTag\": \"${_git_tag_escaped}\",\n")
string(APPEND _json_content "  \"gitTreeState\": \"${_tree_state_escaped}\",\n")
string(APPEND _json_content "  \"buildType\": \"${PLUGINBASED_BUILD_TYPE_escaped}\",\n")
string(APPEND _json_content "  \"platform\": \"${PLUGINBASED_PLATFORM_escaped}\",\n")
string(APPEND _json_content "  \"architecture\": \"${PLUGINBASED_ARCHITECTURE_escaped}\",\n")
string(APPEND _json_content "  \"compiler\": \"${PLUGINBASED_COMPILER_escaped}\",\n")
string(APPEND _json_content "  \"qtVersion\": \"${PLUGINBASED_QT_VERSION_escaped}\"\n")
string(APPEND _json_content "}\n")

pluginbased_write_if_different("${PLUGINBASED_OUTPUT_HEADER}" "${_header_content}")
pluginbased_write_if_different("${PLUGINBASED_OUTPUT_JSON}" "${_json_content}")

message(STATUS
    "Build identity: ${_display_version} (${_tree_state}, ${PLUGINBASED_PLATFORM} ${PLUGINBASED_ARCHITECTURE})")
