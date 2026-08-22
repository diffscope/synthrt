file(GLOB_RECURSE _src "*.cpp")

find_package(stdcorelib CONFIG REQUIRED)
find_package(stdcorelib-plugin CONFIG REQUIRED)

synthrt_add_library(${PROJECT_NAME} SHARED
    SOURCES ${_src}
    LINKS stdcorelib::stdcorelib stdcorelib::plugin
    INCLUDE_PRIVATE ../include/** **
)
