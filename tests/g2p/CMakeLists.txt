# G2P integration tests
#
# Tests for the G2P pipeline: language-aware ModelStep, Multig2p with langRef,
# and per-language ChainG2p package invocation.
#
# Links srt-g2p-headers (G2pTask.h, LangCommon.h) and srt-core (Task.h) so the
# test can construct and inspect G2pInputV1 objects. Full ONNX runtime tests
# require the Multig2p and ChainG2p plugins to be built and loaded.

synthrt_add_cross_test(synthrt-test-g2p-langref tst_g2p_langref.cpp)
target_link_libraries(synthrt-test-g2p-langref PRIVATE
    srt::g2p
    srt::core
)
target_include_directories(synthrt-test-g2p-langref PRIVATE
    ${SYNTHRT_SOURCE_DIR}/include
)

# End-to-end test: Manager + plugin DLLs + real G2P packages (Multig2p ONNX
# inference over all supported languages, plus the ChainG2p Eng/Jpn package
# data regressions). Like every test in the tree it is unified under
# SYNTHRT_BUILD_TESTS (see root CMakeLists note: no separate L2 level).
# Requires real ONNX Runtime + plugin DLLs + package files at TEST time;
# when they are missing the fixture reports setupError and all tests SKIP
# gracefully instead of failing.
synthrt_add_cross_test(synthrt-test-multig2p-languages tst_multig2p_languages.cpp)
target_link_libraries(synthrt-test-multig2p-languages PRIVATE
    srt::g2p
    srt::core
    srt::driver
    srt::svs
)
target_include_directories(synthrt-test-multig2p-languages PRIVATE
    ${SYNTHRT_SOURCE_DIR}/include
)
target_compile_definitions(synthrt-test-multig2p-languages PRIVATE
    SYNTHRT_TEST_SOURCE_DIR="${SYNTHRT_SOURCE_DIR}"
    SYNTHRT_TEST_BINARY_DIR="${CMAKE_BINARY_DIR}"
)
