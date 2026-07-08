project(diffsinger
    VERSION ${DSINFER_VERSION}
    LANGUAGES CXX
)

file(GLOB_RECURSE _src *.h *.cpp)

dsinfer_add_plugin(${PROJECT_NAME} ${CURRENT_PLUGIN_CATEGORY} ${PROJECT_NAME} NO_EXPORT
    SOURCES ${_src}
    FEATURES cxx_std_17
    LINKS srt::core srt-ds::infer
    LINKS_PRIVATE
    INCLUDE_PRIVATE *
)
