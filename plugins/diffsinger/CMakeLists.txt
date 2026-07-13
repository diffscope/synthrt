project(dsinfer-plugins
    VERSION ${PROJECT_VERSION}
    LANGUAGES CXX
)

set(dsinfer-plugins_VAR_PREFIX DSINFER)
set(DSINFER_INSTALL ${SYNTHRT_INSTALL})
set(DSINFER_VERSION ${PROJECT_VERSION})
set(DSINFER_INSTALL_NAME diffsinger)
set(DSINFER_AUTHOR "Team OpenVPI")
set(DSINFER_START_YEAR 2023)
set(DSINFER_INCLUDE_DIR ../../include)
include("${SYNTHRT_SOURCE_DIR}/cmake/BuildAPI.cmake")

set(CURRENT_PLUGIN_CATEGORY inferenceinterpreters)
add_subdirectory(duration)
add_subdirectory(pitch)
add_subdirectory(variance)
add_subdirectory(acoustic)
add_subdirectory(vocoder)

set(CURRENT_PLUGIN_CATEGORY singerproviders)
add_subdirectory(singer-provider)
