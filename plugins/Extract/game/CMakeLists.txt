# synthrt-plugin-extract-game - Game MIDI extractor plugin.
#
# Migrated from ds-editor-lite game-infer. Implements the MidiExtractor
# interface and is loaded by the PluginFactory under IID
# "srt.extract.MidiExtractor" with key "game".

if(NOT TARGET srt::extract)
    message(WARNING "game plugin will not be built: srt::extract target not found.")
    return()
endif()

project(synthrt-plugin-extract-game
    VERSION ${SRT_EXTRACT_VERSION}
    LANGUAGES CXX
)

file(GLOB_RECURSE _src CONFIGURE_DEPENDS *.h *.cpp)

find_package(nlohmann_json CONFIG REQUIRED)

srt_extract_add_plugin(${PROJECT_NAME} MidiExtractor ${PROJECT_NAME} NO_EXPORT
    SOURCES ${_src}
    FEATURES cxx_std_20
    LINKS srt::core srt::driver srt::audio srt::extract
    LINKS_PRIVATE nlohmann_json::nlohmann_json
    INCLUDE_PRIVATE * **
)
