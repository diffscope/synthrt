# diffsinger - DiffSinger singer-provider 插件（提供 DiffSinger 歌声配置）
project(diffsinger
    VERSION ${DSINFER_VERSION}
    LANGUAGES CXX
)

if(NOT TARGET srt-ds::infer)
    message(WARNING "diffsinger will not be built: srt-ds::infer target not found.")
    return()
endif()

file(GLOB_RECURSE _src CONFIGURE_DEPENDS *.h *.cpp)

synthrt_add_plugin(${PROJECT_NAME} ${CURRENT_PLUGIN_CATEGORY} ${PROJECT_NAME} NO_EXPORT
    SOURCES ${_src}
    FEATURES cxx_std_20
    LINKS srt::core srt-ds::infer
    LINKS_PRIVATE
    INCLUDE_PRIVATE *
)
