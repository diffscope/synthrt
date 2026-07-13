if(NOT DEFINED BINARY_DIR OR BINARY_DIR STREQUAL "")
    message(FATAL_ERROR "BINARY_DIR is required")
endif()
if(NOT DEFINED INSTALL_PREFIX OR INSTALL_PREFIX STREQUAL "")
    message(FATAL_ERROR "INSTALL_PREFIX is required")
endif()

get_filename_component(BINARY_DIR "${BINARY_DIR}" ABSOLUTE)
get_filename_component(INSTALL_PREFIX "${INSTALL_PREFIX}" ABSOLUTE)
if(NOT EXISTS "${BINARY_DIR}/cmake_install.cmake")
    message(FATAL_ERROR "BINARY_DIR is not an installable CMake build tree: ${BINARY_DIR}")
endif()

set(_config "")
if(DEFINED TEST_CONFIG AND NOT TEST_CONFIG STREQUAL "")
    set(_config "${TEST_CONFIG}")
elseif(DEFINED CTEST_CONFIGURATION_TYPE AND NOT CTEST_CONFIGURATION_TYPE STREQUAL "")
    set(_config "${CTEST_CONFIGURATION_TYPE}")
elseif(DEFINED ENV{CTEST_CONFIGURATION_TYPE} AND NOT "$ENV{CTEST_CONFIGURATION_TYPE}" STREQUAL "")
    set(_config "$ENV{CTEST_CONFIGURATION_TYPE}")
endif()

file(REMOVE_RECURSE "${INSTALL_PREFIX}")
set(_command "${CMAKE_COMMAND}" --install "${BINARY_DIR}" --prefix "${INSTALL_PREFIX}")
if(NOT _config STREQUAL "")
    list(APPEND _command --config "${_config}")
endif()

execute_process(
    COMMAND ${_command}
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR
        "cmake --install failed with exit code ${_result}\n${_stdout}\n${_stderr}"
    )
endif()
if(NOT EXISTS "${BINARY_DIR}/install_manifest.txt")
    message(FATAL_ERROR "cmake --install did not create ${BINARY_DIR}/install_manifest.txt")
endif()

message(STATUS "Installed ${BINARY_DIR} to ${INSTALL_PREFIX}")
