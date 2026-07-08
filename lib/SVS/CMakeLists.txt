# srt-svs: SVS (singing voice synthesis) runtime library.
#
# v2 Phase 5: extracted from lib/Core/SVS/ as an independent shared library.
# Reverses v1 Phase 2 方案 A (which compiled SVS into srt-core). The 3 .cpp
# files (Inference/InferenceContrib/SingerContrib) move here, the public
# headers under include/synthrt/SVS/ stay unchanged.
project(srt-svs
    LANGUAGES CXX
    VERSION 0.0.1.0
    DESCRIPTION "synthrt SVS (singing voice synthesis) runtime"
)

set(srt-svs_VAR_PREFIX srt_svs)
set(SRT_SVS_INSTALL ${SYNTHRT_INSTALL})
set(SRT_SVS_VERSION ${PROJECT_VERSION})
set(SRT_SVS_INSTALL_NAME ${PROJECT_NAME})
set(SRT_SVS_AUTHOR "Team OpenVPI")
set(SRT_SVS_START_YEAR 2023)
set(SRT_SVS_INCLUDE_DIR ../../include)
include("${SYNTHRT_SOURCE_DIR}/cmake/BuildAPI.cmake")

file(GLOB _src "*.cpp")

find_package(stdcorelib CONFIG REQUIRED)

srt_svs_add_library(${PROJECT_NAME} SHARED PREFIX srt_svs
    SOURCES ${_src}
    LINKS stdcorelib::stdcorelib srt::core
    INCLUDE_PRIVATE ../../include/synthrt/SVS/** **
)
target_compile_definitions(srt-svs PRIVATE SRT_SVS_LIBRARY)

# Convenience aliases matching the v3 roadmap convention (srt::svs).
if(NOT TARGET srt::svs)
    add_library(srt::svs ALIAS srt-svs)
endif()
if(NOT TARGET synthrt::svs)
    add_library(synthrt::svs ALIAS srt-svs)
endif()

srt_svs_install()
