if(NOT TARGET Catch2::Catch2WithMain)
    find_package(Catch2 3 CONFIG REQUIRED)
endif()

add_executable(tst-ds-session test_voicebank_session.cpp)
target_link_libraries(tst-ds-session PRIVATE srt-ds::session Catch2::Catch2WithMain)
include(Catch)
catch_discover_tests(tst-ds-session)

# WP9: hardening regression tests (refresh/snapshot/capability/concurrency contracts).
add_executable(tst-ds-session-hardening test_voicebank_session_hardening.cpp)
target_link_libraries(tst-ds-session-hardening PRIVATE srt-ds::session Catch2::Catch2WithMain)
catch_discover_tests(tst-ds-session-hardening)
