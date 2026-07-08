# Top-level unified tests directory.
#
# Test framework strategy:
#   - Catch2 v3: all synthrt modules (srt-core/srt-ds-bank/srt-driver/srt-g2p/srt-s2p/srt-c/ds-infer)
#
# Per-module tests live in <module>/tests/. This directory hosts cross-module
# integration tests and the Catch2 framework smoke test.

if(NOT TARGET Catch2::Catch2WithMain)
    find_package(Catch2 3 CONFIG REQUIRED)
endif()

include(Catch)

function(synthrt_add_cross_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain)
    set_target_properties(${target} PROPERTIES FOLDER "Tests")
    catch_discover_tests(${target})
endfunction()

add_executable(tst_synthrt_v3_smoke
    Catch2SmokeTest.cpp
)

target_link_libraries(tst_synthrt_v3_smoke PRIVATE Catch2::Catch2WithMain)

# Discover Catch2 test cases
catch_discover_tests(tst_synthrt_v3_smoke)

add_subdirectory(smoke)
add_subdirectory(integration)
add_subdirectory(abi)
add_subdirectory(packaging)
