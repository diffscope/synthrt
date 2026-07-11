# synthrt-plugin-extract-rmvpe - RMVPE pitch extractor plugin.
#
# Migrated from ds-editor-lite rmvpe-infer. Implements the PitchExtractor
# interface and is loaded by the PluginFactory under IID
# "srt.extract.PitchExtractor" with key "rmvpe".

if(NOT TARGET srt::extract)
    message(WARNING "rmvpe plugin will not be built: srt::extract target not found.")
    return()
endif()

project(synthrt-plugin-extract-rmvpe
    VERSION ${SRT_EXTRACT_VERSION}
    LANGUAGES CXX
)

file(GLOB_RECURSE _src CONFIGURE_DEPENDS *.h *.cpp)

srt_extract_add_plugin(${PROJECT_NAME} PitchExtractor ${PROJECT_NAME} NO_EXPORT
    SOURCES ${_src}
    FEATURES cxx_std_20
    LINKS srt::core srt::driver srt::audio srt::extract
    INCLUDE_PRIVATE *
)
