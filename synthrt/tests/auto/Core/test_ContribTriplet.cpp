#include <synthrt/Core/ContribTriplet.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(test_ContribTriplet)

BOOST_AUTO_TEST_CASE(test_triplet_compares_as_one_value) {
    const srt::ContribTriplet triplet("com.example.Contract", 2, "native");
    BOOST_CHECK(triplet == srt::ContribTriplet("com.example.Contract", 2, "native"));
    BOOST_CHECK_EQUAL(triplet.toString(), "com.example.Contract level 2 variant native");

    // Each field alone tells two triplets apart, and the order is total, so a triplet can key a
    // set.
    const srt::ContribTriplet others[] = {
        {"com.example.Other",    2, "native"},
        {"com.example.Contract", 1, "native"},
        {"com.example.Contract", 2, "remote"},
    };
    for (const auto &other : others) {
        BOOST_TEST_CONTEXT(other.toString()) {
            BOOST_CHECK(triplet != other);
            BOOST_CHECK((triplet < other) != (other < triplet));
        }
    }
    BOOST_CHECK(!(triplet < triplet));
}

BOOST_AUTO_TEST_SUITE_END()
