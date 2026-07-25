// Catch2 v3 smoke test for synthrt v3 modules.
//
// This test verifies that the Catch2 v3 test framework is correctly set up.
// It does not depend on any module and serves as a baseline for the test
// infrastructure. Per-module tests will be added in each module's tests/
// directory starting from Phase 1.
//
// Test framework strategy:
//   - Catch2 v3 for all synthrt modules (srt-core/srt-ds-bank/srt-driver/srt-g2p/srt-s2p/srt-c/ds-infer)

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Catch2 v3 is operational", "[smoke]") {
    REQUIRE(1 + 1 == 2);
}

TEST_CASE("Catch2 v3 sections work", "[smoke]") {
    SECTION("basic assertion") {
        REQUIRE(true);
    }
    SECTION("string comparison") {
        std::string s = "synthrt";
        REQUIRE(s == "synthrt");
    }
}
