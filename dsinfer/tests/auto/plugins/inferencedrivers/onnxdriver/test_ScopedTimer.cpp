#include <onnxdriver/Utils/ScopedTimer.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(scope_exit_calls_once_and_deactivation_suppresses_callback) {
    int calls = 0;
    double elapsed = -1;
    {
        ds::onnxdriver::ScopedTimer timer([&](auto duration) {
            ++calls;
            elapsed = duration.count();
        });
        BOOST_CHECK_EQUAL(calls, 0);
    }
    BOOST_CHECK_EQUAL(calls, 1);
    BOOST_CHECK_GE(elapsed, 0);
    {
        ds::onnxdriver::ScopedTimer timer([&](auto) { ++calls; });
        timer.deactivate();
        timer.deactivate();
    }
    BOOST_CHECK_EQUAL(calls, 1);
}
