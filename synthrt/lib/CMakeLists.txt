file(GLOB_RECURSE _src "*.cpp")

find_package(stdcorelib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

synthrt_add_library(${PROJECT_NAME} SHARED
    SOURCES ${_src}
    LINKS stdcorelib::stdcorelib
    LINKS_PRIVATE nlohmann_json::nlohmann_json
    INCLUDE_PRIVATE ../include/** **
)