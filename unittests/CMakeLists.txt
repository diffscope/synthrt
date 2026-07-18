if(NOT TARGET Catch2::Catch2WithMain)
    find_package(Catch2 3 CONFIG REQUIRED)
endif()

include(Catch)

# Common precompiled header for all test targets — heavy headers that
# every test file includes. Reduces per-TU compile time significantly.
set(SYNTHRT_TEST_PCH_HEADERS
    <catch2/catch_test_macros.hpp>
    <string>
    <vector>
    <memory>
    <filesystem>
)

function(synthrt_add_unittest target)
    # Accept one or more source files after the target name.
    set(sources ${ARGN})
    add_executable(${target} ${sources})
    target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain)
    set_target_properties(${target} PROPERTIES FOLDER "Tests")
    # Precompiled header — every test TU includes Catch2 + STL.
    target_precompile_headers(${target} PRIVATE ${SYNTHRT_TEST_PCH_HEADERS})
    # Discover tests for CTest. Set PROCESSORS=1 so ctest -j can pack
    # multiple test binaries onto the same core when I/O bound.
    catch_discover_tests(${target} PROPERTIES PROCESSORS 1)
endfunction()

add_subdirectory(Core)
add_subdirectory(Driver)
add_subdirectory(S2P)
add_subdirectory(G2P)
add_subdirectory(C)

# Parallel test execution: cmake --build build && ctest --test-dir build -j$(nproc)
