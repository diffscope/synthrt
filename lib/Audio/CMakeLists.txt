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

synthrt_declare_target(srt_audio "${PROJECT_NAME}" ${PROJECT_VERSION} "Team OpenVPI" 2023 "include")

file(GLOB_RECURSE _src CONFIGURE_DEPENDS
        "include/*.h"
        "src/*.h"
        "src/*.cpp"
)

find_package(stdcorelib CONFIG REQUIRED)
find_package(FFmpeg REQUIRED)
find_package(SndFile CONFIG REQUIRED)

synthrt_add_library(${PROJECT_NAME} SHARED PREFIX srt_audio
        SOURCES ${_src}
        LINKS stdcorelib::stdcorelib srt::core
        INCLUDE_PRIVATE include/synthrt/Audio/** **
)
target_compile_definitions(srt-audio PRIVATE SRT_AUDIO_LIBRARY)

# FFmpeg/libswresample — private link (PIMPL, headers not exposed in public API)
# FFmpeg 仅用于 FfmpegAudioDecoder（解码侧）；AudioFileWriter 写入侧改用 libsndfile
target_link_libraries(srt-audio PRIVATE ${FFMPEG_LIBRARIES} SndFile::sndfile)
target_include_directories(srt-audio PRIVATE ${FFMPEG_INCLUDE_DIRS})

# Optional xsimd SIMD acceleration (Slicer RMS computation).
if (SYNTHRT_ENABLE_XSIMD)
    find_package(xsimd CONFIG REQUIRED)
    target_link_libraries(srt-audio PRIVATE xsimd)
    target_compile_definitions(srt-audio PRIVATE SRT_AUDIO_ENABLE_XSIMD)
endif ()

# Convenience aliases matching the v3 roadmap convention (srt::audio).
if (NOT TARGET srt::audio)
    add_library(srt::audio ALIAS srt-audio)
endif ()

synthrt_install()
