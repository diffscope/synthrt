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
synthrt_add_unittest(tst-ds-session test_voicebank_session.cpp)
target_link_libraries(tst-ds-session PRIVATE srt-ds::session)

# WP9/WP7 hardening regression tests — split from the former
# test_voicebank_session_hardening.cpp (34 TEST_CASE, 850+ lines) into 5
# themed executables so each compiles in parallel (ninja -j) and runs in
# parallel (ctest -j). Shared helpers live in test_vbs_common.h.
synthrt_add_unittest(tst-ds-session-refresh    test_vbs_refresh.cpp)
target_link_libraries(tst-ds-session-refresh    PRIVATE srt-ds::session)

synthrt_add_unittest(tst-ds-session-snapshot    test_vbs_snapshot.cpp)
target_link_libraries(tst-ds-session-snapshot    PRIVATE srt-ds::session)

synthrt_add_unittest(tst-ds-session-capability  test_vbs_capability.cpp)
target_link_libraries(tst-ds-session-capability  PRIVATE srt-ds::session)

synthrt_add_unittest(tst-ds-session-modelset    test_vbs_modelset.cpp)
target_link_libraries(tst-ds-session-modelset    PRIVATE srt-ds::session)

synthrt_add_unittest(tst-ds-session-language    test_vbs_language.cpp)
target_link_libraries(tst-ds-session-language    PRIVATE srt-ds::session)

# Snapshot / ensure API coverage supplement (V3-06 ~ V3-12, D-26 ~ D-36):
# SessionResources injection, config delayed effect, refresh failure preserves
# previous snapshot, Discovery multi-package partial success, ensureLanguageReady
# state machine, subscribeRefresh edge cases (multi-subscriber / exception
# isolation / external reset / move semantics), fingerprint order-independence,
# sync/refreshAsync concurrency, Stale retry L2 placeholder. Reuses shared
# fixtures from test_vbs_common.h.
synthrt_add_unittest(tst-ds-session-snapshot-ensure test_voicebank_session_snapshot_ensure.cpp)
target_link_libraries(tst-ds-session-snapshot-ensure PRIVATE srt-ds::session)
