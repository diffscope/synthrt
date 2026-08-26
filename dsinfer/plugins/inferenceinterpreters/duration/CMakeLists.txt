project(duration
    VERSION ${DSINFER_VERSION}
    LANGUAGES CXX
)

file(GLOB_RECURSE _src *.h *.cpp)

find_package(stduuid CONFIG REQUIRED)

dsinfer_add_plugin(${PROJECT_NAME} CATEGORY ${CURRENT_PLUGIN_CATEGORY}/duration NO_EXPORT
    SOURCES ${_src}
    FEATURES cxx_std_17
    LINKS dsinfer
    LINKS_PRIVATE stduuid $<BUILD_INTERFACE:inferutil>
    INCLUDE_PRIVATE *
)

dsinfer_add_plugin_metadata(${PROJECT_NAME} ${CURRENT_PLUGIN_IID})
