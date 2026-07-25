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
add_subdirectory(SVS)
add_subdirectory(Audio)
add_subdirectory(Extract)

# TD-CLI-01: tools/ CLI 工具测试基础设施（L2 黑盒 subprocess）。
# 仅在 SYNTHRT_BUILD_TOOLS=ON 时构建，避免无 CLI 可执行文件时链接失败。
if(SYNTHRT_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# Parallel test execution: cmake --build build && ctest --test-dir build -j$(nproc)
