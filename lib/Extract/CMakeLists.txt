project(srt-extract
    LANGUAGES CXX
    VERSION ${SYNTHRT_VERSION}
    DESCRIPTION "synthrt v3 audio feature extractors (pitch/MIDI)"
)

set(srt-extract_VAR_PREFIX srt_extract)
set(SRT_EXTRACT_INSTALL ${SYNTHRT_INSTALL})
set(SRT_EXTRACT_VERSION ${PROJECT_VERSION})
set(SRT_EXTRACT_INSTALL_NAME ${PROJECT_NAME})
set(SRT_EXTRACT_AUTHOR "Team OpenVPI")
set(SRT_EXTRACT_START_YEAR 2026)
set(SRT_EXTRACT_INCLUDE_DIR include)
include("${SYNTHRT_SOURCE_DIR}/cmake/BuildAPI.cmake")

# srt-extract library
#
# Provides PitchExtractor / MidiExtractor interfaces and shared preprocessing
# helpers (AudioPreprocessor, getInferenceDriver). Concrete model
# implementations (rmvpe, game) live in plugins/Extract/.
#
# Depends on srt::audio (Phase 1, lib/Audio) for AudioBuffer/Slicer/
# SwresampleResampler and srt::driver for InferenceDriver.

file(GLOB_RECURSE _src CONFIGURE_DEPENDS "src/*.cpp")

find_package(stdcorelib CONFIG REQUIRED)

srt_extract_add_library(${PROJECT_NAME} SHARED
    SOURCES ${_src}
    LINKS stdcorelib::stdcorelib srt::core srt::audio srt::driver
    INCLUDE_PRIVATE include/synthrt/Extract/** **
)

# Convenience alias matching the v3 roadmap convention (srt::extract).
if(NOT TARGET srt::extract)
    add_library(srt::extract ALIAS ${PROJECT_NAME})
endif()
if(NOT TARGET synthrt::extract)
    add_library(synthrt::extract ALIAS ${PROJECT_NAME})
endif()

srt_extract_install()
