# synthrt_unittest.cmake
#
# Shared helper for building synthrt test executables across all test
# directories (top-level unittests/ and per-domain unittests/ folders such
# as domains/ds-session/unittests/).
#
# Provides synthrt_add_unittest(<target> <sources...>) which:
#   - links Catch2::Catch2WithMain
#   - sets a precompiled header of heavy STL + Catch2 headers
#   - registers tests with ctest via catch_discover_tests (PROCESSORS=1
#     so ctest -j can pack multiple I/O-bound test binaries per core)
#
# This file is intentionally side-effect free apart from defining the
# variable and the function: it does NOT find_package(Catch2) or
# include(Catch). Callers must ensure Catch2::Catch2WithMain exists and
# the Catch module has been included before invoking synthrt_add_unittest.
#
# The file is include()d once from the root CMakeLists.txt (inside the
# SYNTHRT_BUILD_TESTS guard, before any add_subdirectory that contains a
# unittests/ folder) so the function is in scope for every test directory
# regardless of traversal order.

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
