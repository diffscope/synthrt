project(dsinfer
    LANGUAGES CXX
    VERSION 0.0.1.3
    DESCRIPTION "DiffSinger Inference Framework"
)

# ----------------------------------
# Build Options
# ----------------------------------
option(DSINFER_ENABLE_DIRECTML "Enable DirectML provider" ON)
option(DSINFER_ENABLE_CUDA "Enable CUDA provider" ON)
option(DSINFER_ENABLE_STATIC_PLUGINS "Enable static plugin linking" OFF) # TODO: Implement this option

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

add_subdirectory(util)

add_subdirectory(plugins)

add_subdirectory(tools)

if(SYNTHRT_BUILD_TESTS)
    add_subdirectory(tests)
endif()

dsinfer_install()