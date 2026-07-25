# acoustic - DiffSinger acoustic stage 推理插件
project(acoustic
    VERSION ${DSINFER_VERSION}
    LANGUAGES CXX
)

if(NOT TARGET srt-ds::infer)
    message(WARNING "acoustic will not be built: srt-ds::infer target not found.")
    return()
endif()

file(GLOB_RECURSE _src CONFIGURE_DEPENDS *.h *.cpp)

find_package(stduuid CONFIG REQUIRED)

synthrt_add_plugin(${PROJECT_NAME} ${CURRENT_PLUGIN_CATEGORY} ${PROJECT_NAME} NO_EXPORT
    SOURCES ${_src}
    FEATURES cxx_std_20
    LINKS srt::core srt::driver srt-ds::infer
    LINKS_PRIVATE stduuid $<BUILD_INTERFACE:inferutil>
    INCLUDE_PRIVATE *
)
