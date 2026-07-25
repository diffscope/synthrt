project(srt-extract
    LANGUAGES CXX
    VERSION ${SYNTHRT_VERSION}
    DESCRIPTION "synthrt v3 audio feature extractors (pitch/MIDI)"
)

synthrt_declare_target(srt_extract "${PROJECT_NAME}" ${PROJECT_VERSION} "Team OpenVPI" 2026 "include")

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

synthrt_add_library(${PROJECT_NAME} SHARED
    SOURCES ${_src}
    LINKS stdcorelib::stdcorelib srt::core srt::audio srt::driver
    INCLUDE_PRIVATE include/synthrt/Extract/** **
)

# Convenience alias matching the v3 roadmap convention (srt::extract).
if(NOT TARGET srt::extract)
    add_library(srt::extract ALIAS ${PROJECT_NAME})
endif()

synthrt_install()
