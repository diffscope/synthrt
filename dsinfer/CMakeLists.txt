project(dsinfer
    LANGUAGES CXX
    VERSION 0.0.1.0
    DESCRIPTION "DiffSinger Inference Framework"
)

# ----------------------------------
# Build Options
# ----------------------------------
option(DSINFER_ENABLE_DIRECTML "Enable DirectML provider" OFF)
option(DSINFER_ENABLE_CUDA "Enable CUDA provider" OFF)

# ----------------------------------
# Project Variables
# ----------------------------------
set(DSINFER_INSTALL ${SYNTHRT_INSTALL})
set(DSINFER_VERSION ${PROJECT_VERSION})
set(DSINFER_INSTALL_NAME ${PROJECT_NAME})

# ----------------------------------
# Configure BuildAPI
# ----------------------------------
set(DSINFER_AUTHOR "Team OpenVPI")
set(DSINFER_START_YEAR 2023)
set(DSINFER_INCLUDE_DIR include)
include("${SYNTHRT_SOURCE_DIR}/cmake/BuildAPI.cmake")

# ----------------------------------
# Add source modules
# ----------------------------------
add_subdirectory(lib)

add_subdirectory(plugins)

if(SYNTHRT_BUILD_TESTS)
    add_subdirectory(tests)
endif()

dsinfer_install()