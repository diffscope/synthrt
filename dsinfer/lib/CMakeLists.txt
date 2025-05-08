file(GLOB_RECURSE _src "*.cpp")

dsinfer_add_library(${PROJECT_NAME} SHARED
    SOURCES ${_src}
    LINKS synthrt
    LINKS_PRIVATE
    INCLUDE_PRIVATE ../include/** **
)

find_path(SPARSEPP_INCLUDE_DIRS "sparsepp/spp.h")
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${SPARSEPP_INCLUDE_DIRS}>
)