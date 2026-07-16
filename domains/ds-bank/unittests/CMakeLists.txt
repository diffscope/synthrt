if(NOT TARGET Catch2::Catch2WithMain)
    find_package(Catch2 3 CONFIG REQUIRED)
endif()

file(GLOB_RECURSE _src CONFIGURE_DEPENDS "*.cpp")

add_executable(tst-ds-bank ${_src})
target_link_libraries(tst-ds-bank PRIVATE srt-ds::bank srt::g2p Catch2::Catch2WithMain)
# SingerCapabilityAnalyzer.h is a private header in lib/, expose it to tests.
target_include_directories(tst-ds-bank PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../lib)
set_target_properties(tst-ds-bank PROPERTIES FOLDER "Tests")

include(CTest)
include(Catch)
catch_discover_tests(tst-ds-bank)
