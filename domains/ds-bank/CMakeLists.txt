project(dsbank
    LANGUAGES CXX
    VERSION 0.0.1.0
    DESCRIPTION "synthrt v3 package management"
)

# ----------------------------------
# Configure BuildAPI
# ----------------------------------
synthrt_declare_target(dsbank "srt-ds-bank" ${PROJECT_VERSION} "Team OpenVPI" 2023 "../../include")

# ----------------------------------
# Add source modules (enabled in Phase 2a)
# ----------------------------------
set(CMAKE_FOLDER "Domains")
add_subdirectory(lib)
if(SYNTHRT_BUILD_TESTS)
    set(CMAKE_FOLDER "Tests")
    add_subdirectory(unittests)
endif()

synthrt_install()
