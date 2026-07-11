# plugins/Extract - Extractor plugins (pitch/MIDI)
#
# Phase 2: empty shell. Concrete extractor plugins are added in later phases:
#   - Phase 3: rmvpe (PitchExtractor plugin)
#   - Phase 4: game (MidiExtractor plugin)
#
# The BuildAPI setup below is provided so that srt_extract_add_plugin() is
# available once the subdirectories are uncommented.

project(srt-extract-plugins
    VERSION ${SYNTHRT_VERSION}
    LANGUAGES CXX
)

set(srt-extract-plugins_VAR_PREFIX srt_extract)
set(srt-extract_VAR_PREFIX srt_extract)
set(SRT_EXTRACT_INSTALL ${SYNTHRT_INSTALL})
set(SRT_EXTRACT_VERSION ${SYNTHRT_VERSION})
set(SRT_EXTRACT_INSTALL_NAME srt-extract)
set(SRT_EXTRACT_AUTHOR "Team OpenVPI")
set(SRT_EXTRACT_START_YEAR 2026)
set(SRT_EXTRACT_INCLUDE_DIR ../../include)
include("${SYNTHRT_SOURCE_DIR}/cmake/BuildAPI.cmake")

# Phase 3: pitch extractor plugin (rmvpe)
add_subdirectory(rmvpe)

# Phase 4: MIDI extractor plugin (game)
add_subdirectory(game)
