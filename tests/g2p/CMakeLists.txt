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
