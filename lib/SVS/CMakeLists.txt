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

synthrt_declare_target(srt_svs "${PROJECT_NAME}" ${PROJECT_VERSION} "Team OpenVPI" 2023 "../../include")

file(GLOB _src "*.cpp")

find_package(stdcorelib CONFIG REQUIRED)

synthrt_add_library(${PROJECT_NAME} SHARED PREFIX srt_svs
    SOURCES ${_src}
    LINKS stdcorelib::stdcorelib srt::core
    INCLUDE_PRIVATE ../../include/synthrt/SVS/** **
)
target_compile_definitions(srt-svs PRIVATE SRT_SVS_LIBRARY)

# Convenience aliases matching the v3 roadmap convention (srt::svs).
if(NOT TARGET srt::svs)
    add_library(srt::svs ALIAS srt-svs)
endif()

synthrt_install()
