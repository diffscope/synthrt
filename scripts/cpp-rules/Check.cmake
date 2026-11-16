cmake_minimum_required(VERSION 3.19)

# REPO_ROOT and BUILD_DIR are inputs, so the policy does not depend on a project
# name or layout. FILES is an explicit semicolon-separated list relative to root.
# No recursive scan is used. Callers select owned files and exclude vendor code.
if(NOT DEFINED REPO_ROOT)
    get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
set(_policy "${CMAKE_CURRENT_LIST_DIR}")
if(SYNC_FORMAT)
    configure_file("${_policy}/.clang-format" "${REPO_ROOT}/.clang-format" COPYONLY)
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
    "${_policy}/.clang-format" "${REPO_ROOT}/.clang-format" RESULT_VARIABLE _different)
if(_different)
    message(FATAL_ERROR "Format configuration differs. Run with -DSYNC_FORMAT=ON.")
endif()
find_program(_format NAMES clang-format REQUIRED)
find_program(_tidy NAMES clang-tidy REQUIRED)
execute_process(COMMAND "${_tidy}" --verify-config "--config-file=${_policy}/.clang-tidy"
    RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Invalid clang-tidy configuration")
endif()
foreach(_file IN LISTS FILES)
    get_filename_component(_absolute "${_file}" ABSOLUTE BASE_DIR "${REPO_ROOT}")
    execute_process(COMMAND "${_format}" --dry-run --Werror
        "--style=file:${_policy}/.clang-format" "${_absolute}" RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Format check failed: ${_file}")
    endif()
    if(_file MATCHES "\\.cpp$")
        if(NOT DEFINED BUILD_DIR)
            message(FATAL_ERROR "BUILD_DIR must contain compile_commands.json for C++ checks")
        endif()
        if(NOT EXISTS "${BUILD_DIR}/compile_commands.json")
            message(FATAL_ERROR "Missing compilation database: ${BUILD_DIR}/compile_commands.json")
        endif()
        # HEADER_FILTER is supplied by the repository to include owned headers
        # and exclude dependency headers. Requiring it prevents silent omissions.
        if(NOT DEFINED HEADER_FILTER)
            message(FATAL_ERROR "HEADER_FILTER must select owned project headers")
        endif()
        execute_process(COMMAND "${_tidy}" "${_absolute}" "-p=${BUILD_DIR}"
            "--config-file=${_policy}/.clang-tidy" "--header-filter=${HEADER_FILTER}"
            RESULT_VARIABLE _result)
        if(NOT _result EQUAL 0)
            message(FATAL_ERROR "C++ check failed: ${_file}")
        endif()
    endif()
endforeach()
