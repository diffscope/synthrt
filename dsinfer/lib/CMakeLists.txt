file(GLOB_RECURSE _src "*.cpp")

# The export macros would take their prefix from the target name, SYNTHRT_DSINFER, while
# dsinfer_global.h tests DSINFER_LIBRARY and DSINFER_STATIC.
dsinfer_add_library(${PROJECT_NAME} SHARED
    MACRO_PREFIX DSINFER
    SOURCES ${_src}
    LINKS synthrt
    LINKS_PRIVATE
    INCLUDE_PRIVATE ../include/** **
)

# The package is synthrt-dsinfer, and the namespace is synthrt, which the libraries built on synthrt
# share, so that a consumer links synthrt::dsinfer.
set_target_properties(${PROJECT_NAME} PROPERTIES EXPORT_NAME dsinfer)

find_path(SPARSEPP_INCLUDE_DIRS "sparsepp/spp.h")
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${SPARSEPP_INCLUDE_DIRS}>
)
