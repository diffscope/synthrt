#include <cstdint>

#include <inferutil/Speedup.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(step_counts_select_a_bounded_divisor) {
    using ds::inferutil::getSpeedupFromSteps;
    BOOST_CHECK_EQUAL(getSpeedupFromSteps(0), 10);
    BOOST_CHECK_EQUAL(getSpeedupFromSteps(-1, 25), 25);
    BOOST_CHECK_EQUAL(getSpeedupFromSteps(1), 1000);
    BOOST_CHECK_EQUAL(getSpeedupFromSteps(1001), 1);
    BOOST_CHECK_EQUAL(getSpeedupFromSteps(30), 25);
    for (int64_t steps = 1; steps <= 2000; ++steps) {
        const auto speedup = getSpeedupFromSteps(steps);
        BOOST_REQUIRE_GE(speedup, 1);
        BOOST_REQUIRE_LE(speedup, 1000);
        BOOST_CHECK_EQUAL(1000 % speedup, 0);
    }
}
