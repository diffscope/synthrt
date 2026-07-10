project(duration
    VERSION ${DSINFER_VERSION}
    LANGUAGES CXX
)

file(GLOB_RECURSE _src *.h *.cpp)

find_package(stduuid CONFIG REQUIRED)

dsinfer_add_plugin(${PROJECT_NAME} ${CURRENT_PLUGIN_CATEGORY} ${PROJECT_NAME} NO_EXPORT
    SOURCES ${_src}
    FEATURES cxx_std_20
    LINKS srt::core srt::driver srt-ds::infer
    LINKS_PRIVATE stduuid $<BUILD_INTERFACE:inferutil>
    INCLUDE_PRIVATE *
)
