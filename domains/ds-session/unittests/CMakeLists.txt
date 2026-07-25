if(NOT TARGET Catch2::Catch2WithMain)
    find_package(Catch2 3 CONFIG REQUIRED)
endif()
include(Catch)

# synthrt_add_unittest is defined in cmake/synthrt_unittest.cmake (included
# once from the root CMakeLists.txt). It links Catch2::Catch2WithMain, sets
# the shared PCH, and calls catch_discover_tests; we only need to add the
# domain-specific srt-ds::session link on top.

# Original test file (kept as a single TU; 461 lines / 23 cases is below
# the split threshold of 500 lines / 15 cases).
synthrt_add_unittest(tst-ds-session tst_voicebank_session.cpp)
target_link_libraries(tst-ds-session PRIVATE srt-ds::session)

# WP9/WP7 hardening regression tests — split from the former
# tst_voicebank_session_hardening.cpp (34 TEST_CASE, 850+ lines) into 5
# themed executables so each compiles in parallel (ninja -j) and runs in
# parallel (ctest -j). Shared helpers live in test_vbs_common.h.
synthrt_add_unittest(tst-ds-session-refresh    tst_vbs_refresh.cpp)
target_link_libraries(tst-ds-session-refresh    PRIVATE srt-ds::session)

synthrt_add_unittest(tst-ds-session-snapshot    tst_vbs_snapshot.cpp)
target_link_libraries(tst-ds-session-snapshot    PRIVATE srt-ds::session)

synthrt_add_unittest(tst-ds-session-capability  tst_vbs_capability.cpp)
target_link_libraries(tst-ds-session-capability  PRIVATE srt-ds::session)

synthrt_add_unittest(tst-ds-session-modelset    tst_vbs_modelset.cpp)
target_link_libraries(tst-ds-session-modelset    PRIVATE srt-ds::session)

synthrt_add_unittest(tst-ds-session-language    tst_vbs_language.cpp)
target_link_libraries(tst-ds-session-language    PRIVATE srt-ds::session)

# Snapshot / ensure API coverage supplement (V3-06 ~ V3-12, D-26 ~ D-36):
# SessionResources injection, config delayed effect, refresh failure preserves
# previous snapshot, Discovery multi-package partial success, ensureLanguageReady
# state machine, subscribeRefresh edge cases (multi-subscriber / exception
# isolation / external reset / move semantics), fingerprint order-independence,
# sync/refreshAsync concurrency, Stale retry L2 placeholder. Reuses shared
# fixtures from test_vbs_common.h.
synthrt_add_unittest(tst-ds-session-snapshot-ensure tst_voicebank_session_snapshot_ensure.cpp)
target_link_libraries(tst-ds-session-snapshot-ensure PRIVATE srt-ds::session)

# Edge-case matrix (test-matrix-expansion.md §1.10): SES-001 ~ SES-012.
# Reuses test_vbs_common.h fixtures (makeRoot/makePackage/makeSecondPackage).
# Cases requiring real ONNX models or non-default ResolutionState use SKIP()
# (SES-005).
synthrt_add_unittest(tst-ds-session-edge tst_session_edge.cpp)
target_link_libraries(tst-ds-session-edge PRIVATE srt-ds::session)

# S1-S5 lifecycle API tests: loadVoicebank / unloadVoicebank /
# loadedVoicebanks / Error::withExtraContext / formatContext /
# SessionResources.g2pPluginPaths. L1 tests exercising error paths and
# basic contracts without real ONNX models or G2P plugins.
synthrt_add_unittest(tst-ds-session-lifecycle tst_vbs_lifecycle.cpp)
target_link_libraries(tst-ds-session-lifecycle PRIVATE srt-ds::session)

# T-05 VoicebankSession 并发刷新测试 (BF-58 回归, P2)：
# generation 不变量（no-op refresh 不 bump generation）、并发 refreshAsync
# 合并到相同 generation、sync/async 跨边界合并、高争用压力、snapshot
# 读一致性。L1 fixture 覆盖 generation 机制；createModelSet + StaleModelSet
# 路径以 SKIP 占位（需 L2 ONNX fixture）。
synthrt_add_unittest(tst-ds-session-concurrent tst_vbs_concurrent.cpp)
target_link_libraries(tst-ds-session-concurrent PRIVATE srt-ds::session)

# A2 VoicebankSnapshot const query methods (lite integration):
# findSinger / findSingersBySingerId / findPackage / findManifest.
# Pure L1 tests: manually populate a VoicebankSnapshot struct and exercise
# the const query methods. No Runtime / ONNX runtime / plugin DLL needed.
synthrt_add_unittest(tst-ds-session-snapshot-query tst_vbs_snapshot_query.cpp)
target_link_libraries(tst-ds-session-snapshot-query PRIVATE srt-ds::session)

# L2 end-to-end tests (real plugins) gated behind SYNTHRT_ENABLE_L2_TESTS
# (INFRA-03 测试分级). When ON, the macro is defined for this target so
# L2 test cases gated by #ifdef SYNTHRT_ENABLE_L2_TESTS are compiled.
# Fixtures/helper are added by ET-01 (docs/refactoring/04-edge-tests/plan.md).
if(SYNTHRT_ENABLE_L2_TESTS)
    target_compile_definitions(tst-ds-session-lifecycle PRIVATE SYNTHRT_ENABLE_L2_TESTS)
endif()
