# plugins/Extract - Extractor plugins (pitch/MIDI)
#
# Phase 2: empty shell. Concrete extractor plugins are added in later phases:
#   - Phase 3: rmvpe (PitchExtractor plugin)
#   - Phase 4: game (MidiExtractor plugin)
#
# The BuildAPI setup above is provided so that synthrt_add_plugin() is
# available for the subdirectories below.

project(srt-extract-plugins
    VERSION ${SYNTHRT_VERSION}
    LANGUAGES CXX
)

synthrt_declare_target(srt_extract "srt-extract" ${SYNTHRT_VERSION} "Team OpenVPI" 2026 "../../include")

# Phase 3: pitch extractor plugin (rmvpe)
add_subdirectory(rmvpe)

# Phase 4: MIDI extractor plugin (game)
add_subdirectory(game)
