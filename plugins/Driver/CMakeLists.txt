project(srt-driver-plugins
    VERSION ${PROJECT_VERSION}
    LANGUAGES CXX
)

set(srt-driver-plugins_VAR_PREFIX srt_driver)
set(srt-driver_VAR_PREFIX srt_driver)
set(SRT_DRIVER_INSTALL ${SYNTHRT_INSTALL})
set(SRT_DRIVER_VERSION ${PROJECT_VERSION})
set(SRT_DRIVER_INSTALL_NAME srt-driver)
set(SRT_DRIVER_AUTHOR "Team OpenVPI")
set(SRT_DRIVER_START_YEAR 2023)
set(SRT_DRIVER_INCLUDE_DIR ../../include)
include("${SYNTHRT_SOURCE_DIR}/cmake/BuildAPI.cmake")

add_subdirectory(onnx)
