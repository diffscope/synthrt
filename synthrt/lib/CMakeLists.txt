file(GLOB_RECURSE _src "*.cpp")

find_package(stdcorelib CONFIG REQUIRED)

synthrt_add_library(${PROJECT_NAME} SHARED
    SOURCES ${_src}
    LINKS stdcorelib::stdcorelib
    INCLUDE_PRIVATE ../include/** **
)
