project(dsbank
    LANGUAGES CXX
    VERSION 0.0.1.0
    DESCRIPTION "synthrt v3 package management"
)

# ----------------------------------
# Project Variables
# ----------------------------------
set(DSBANK_INSTALL ${SYNTHRT_INSTALL})
set(DSBANK_VERSION ${PROJECT_VERSION})
set(DSBANK_INSTALL_NAME ${PROJECT_NAME})

# ----------------------------------
# Configure BuildAPI
# ----------------------------------
set(DSBANK_AUTHOR "Team OpenVPI")
set(DSBANK_START_YEAR 2023)
set(DSBANK_INCLUDE_DIR ../../include)
include("${SYNTHRT_SOURCE_DIR}/cmake/BuildAPI.cmake")

# ----------------------------------
# Add source modules (enabled in Phase 2a)
# ----------------------------------
set(CMAKE_FOLDER "Domains")
add_subdirectory(lib)
if(SYNTHRT_BUILD_TESTS)
    set(CMAKE_FOLDER "Tests")
    add_subdirectory(unittests)
endif()

dsbank_install()
