project(srt-core
    LANGUAGES CXX
    VERSION 0.0.1.0
    DESCRIPTION "synthrt v3 core infrastructure"
)

synthrt_declare_target(srt_core "${PROJECT_NAME}" ${PROJECT_VERSION} "Team OpenVPI" 2023 "../../include")

file(GLOB_RECURSE _src CONFIGURE_DEPENDS "*.cpp")
# v2 Phase 5: SVS .cpp moved to lib/SVS/ (independent srt-svs target).
list(FILTER _src EXCLUDE REGEX "/SVS/.+\\.cpp$")

find_package(stdcorelib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

synthrt_add_library(${PROJECT_NAME} SHARED
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

synthrt_install()
