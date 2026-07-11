# srt-audio: Audio processing library (FFmpeg-based).
#
# Phase 1: migrated from dataset-tools src/framework/audio/ (FFmpeg-based)
# and ds-editor-lite audio-util (Slicer). Provides AudioBuffer, decoders,
# resamplers, pipeline, file writer and RMS slicer under namespace srt::audio.
project(srt-audio
        LANGUAGES CXX
        VERSION 0.0.1.0
        DESCRIPTION "synthrt audio processing library (FFmpeg-based)"
)

set(srt-audio_VAR_PREFIX srt_audio)
set(SRT_AUDIO_INSTALL ${SYNTHRT_INSTALL})
set(SRT_AUDIO_VERSION ${PROJECT_VERSION})
set(SRT_AUDIO_INSTALL_NAME ${PROJECT_NAME})
set(SRT_AUDIO_AUTHOR "Team OpenVPI")
set(SRT_AUDIO_START_YEAR 2023)
set(SRT_AUDIO_INCLUDE_DIR include)
include("${SYNTHRT_SOURCE_DIR}/cmake/BuildAPI.cmake")

file(GLOB_RECURSE _src CONFIGURE_DEPENDS
        "include/*.h"
        "src/*.h"
        "src/*.cpp"
)

find_package(stdcorelib CONFIG REQUIRED)
find_package(FFmpeg REQUIRED)

srt_audio_add_library(${PROJECT_NAME} SHARED PREFIX srt_audio
        SOURCES ${_src}
        LINKS stdcorelib::stdcorelib srt::core
        INCLUDE_PRIVATE include/synthrt/Audio/** **
)
target_compile_definitions(srt-audio PRIVATE SRT_AUDIO_LIBRARY)

# FFmpeg/libswresample — private link (PIMPL, headers not exposed in public API)
target_link_libraries(srt-audio PRIVATE ${FFMPEG_LIBRARIES})
target_include_directories(srt-audio PRIVATE ${FFMPEG_INCLUDE_DIRS})

# Optional: xsimd SIMD acceleration (Slicer RMS computation)
find_package(xsimd CONFIG)
if (xsimd_FOUND)
    target_link_libraries(srt-audio PRIVATE xsimd)
    target_compile_definitions(srt-audio PRIVATE SRT_AUDIO_ENABLE_XSIMD)
endif ()

# Convenience aliases matching the v3 roadmap convention (srt::audio).
if (NOT TARGET srt::audio)
    add_library(srt::audio ALIAS srt-audio)
endif ()
if (NOT TARGET synthrt::audio)
    add_library(synthrt::audio ALIAS srt-audio)
endif ()

srt_audio_install()
