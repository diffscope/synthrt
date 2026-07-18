if(NOT TARGET Catch2::Catch2WithMain)
    find_package(Catch2 3 CONFIG REQUIRED)
endif()

include(Catch)

# synthrt_add_unittest() and SYNTHRT_TEST_PCH_HEADERS are defined in
# cmake/synthrt_unittest.cmake, which is include()d once from the root
# CMakeLists.txt (inside the SYNTHRT_BUILD_TESTS guard, before any
# add_subdirectory that contains a unittests/ folder). The definition is
# shared with per-domain test directories such as domains/ds-session/unittests.

add_subdirectory(Core)
add_subdirectory(Driver)
add_subdirectory(S2P)
add_subdirectory(G2P)
add_subdirectory(C)

# Parallel test execution: cmake --build build && ctest --test-dir build -j$(nproc)
