# G2P integration tests
#
# Tests for the G2P pipeline: language-aware ModelStep, Multig2p with langRef,
# and per-language ChainG2p package invocation.
#
# All tests are unified under SYNTHRT_BUILD_TESTS (no separate L2 level).
# Full ONNX runtime tests require the Multig2p and ChainG2p plugins to be
# built and loaded, and the Phonetic-Suite-Multi package.

synthrt_add_cross_test(synthrt-test-g2p-langref tst_g2p_langref.cpp)
target_link_libraries(synthrt-test-g2p-langref PRIVATE
    srt::g2p
    srt::core
)
target_include_directories(synthrt-test-g2p-langref PRIVATE
    ${SYNTHRT_SOURCE_DIR}/include
)

synthrt_add_cross_test(synthrt-test-multig2p-languages tst_multig2p_languages.cpp)
target_link_libraries(synthrt-test-multig2p-languages PRIVATE
    srt::g2p
    srt::core
    srt::driver
)
target_include_directories(synthrt-test-multig2p-languages PRIVATE
    ${SYNTHRT_SOURCE_DIR}/include
)
target_compile_definitions(synthrt-test-multig2p-languages PRIVATE
    SYNTHRT_TEST_SOURCE_DIR="${SYNTHRT_SOURCE_DIR}"
    SYNTHRT_TEST_BINARY_DIR="${CMAKE_BINARY_DIR}"
)
