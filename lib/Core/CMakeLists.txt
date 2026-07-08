project(srt-core
    LANGUAGES CXX
    VERSION 0.0.1.0
    DESCRIPTION "synthrt v3 core infrastructure"
)

set(srt-core_VAR_PREFIX srt_core)
set(SRT_CORE_INSTALL ${SYNTHRT_INSTALL})
set(SRT_CORE_VERSION ${PROJECT_VERSION})
set(SRT_CORE_INSTALL_NAME ${PROJECT_NAME})
set(SRT_CORE_AUTHOR "Team OpenVPI")
set(SRT_CORE_START_YEAR 2023)
set(SRT_CORE_INCLUDE_DIR ../../include)
include("${SYNTHRT_SOURCE_DIR}/cmake/BuildAPI.cmake")

file(GLOB_RECURSE _src CONFIGURE_DEPENDS "*.cpp")
# v2 Phase 5: SVS .cpp moved to lib/SVS/ (independent srt-svs target).
list(FILTER _src EXCLUDE REGEX "/SVS/.+\\.cpp$")

find_package(stdcorelib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

srt_core_add_library(${PROJECT_NAME} SHARED
    SOURCES ${_src}
    LINKS stdcorelib::stdcorelib nlohmann_json::nlohmann_json
    INCLUDE_PRIVATE ../../include/synthrt/Core/** **
)

# Convenience alias matching the v3 roadmap convention (srt::core, srt::driver, ...).
# BuildAPI creates ${INSTALL_NAME}::${name} (srt-core::srt-core); add the short
# namespace-style alias expected by downstream modules.
if(NOT TARGET srt::core)
    add_library(srt::core ALIAS ${PROJECT_NAME})
endif()
if(NOT TARGET synthrt::core)
    add_library(synthrt::core ALIAS ${PROJECT_NAME})
endif()

srt_core_install()
