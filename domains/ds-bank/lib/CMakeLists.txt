file(GLOB_RECURSE _src CONFIGURE_DEPENDS "*.cpp")

find_package(stdcorelib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# srt-core is built in-tree (added before srt-ds-bank in the top-level CMakeLists.txt),
# so the srt::core alias target is available directly — no find_package needed.
dsbank_add_library(srt-ds-bank SHARED PREFIX dsbank
    SOURCES ${_src}
    LINKS stdcorelib::stdcorelib srt::core
    LINKS_PRIVATE nlohmann_json::nlohmann_json
    INCLUDE_PRIVATE ../../../include/diffsinger/Bank/** **
)
target_compile_definitions(srt-ds-bank PRIVATE DSBANK_LIBRARY)

if(NOT TARGET srt-ds::bank)
    add_library(srt-ds::bank ALIAS srt-ds-bank)
endif()
