project(acoustic
    VERSION ${DSINFER_VERSION}
    LANGUAGES CXX
)

file(GLOB_RECURSE _src *.h *.cpp)

find_package(stduuid CONFIG REQUIRED)

dsinfer_add_plugin(${PROJECT_NAME} ${CURRENT_PLUGIN_CATEGORY} NO_EXPORT
    SOURCES ${_src}
    FEATURES cxx_std_17
    LINKS dsinfer
    LINKS_PRIVATE stduuid $<BUILD_INTERFACE:InterpreterCommon>
    INCLUDE_PRIVATE *
)