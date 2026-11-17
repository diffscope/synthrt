#define BOOST_TEST_MAIN
#include "TestSupport.h"

BOOST_AUTO_TEST_CASE(test_linked_categories_are_instantiated_for_each_unit) {
    srt::SynthUnit first;
    srt::SynthUnit second;

    BOOST_REQUIRE(first.category(testCategoryName));
    BOOST_REQUIRE(second.category(testCategoryName));
    BOOST_CHECK(first.category(testCategoryName) != second.category(testCategoryName));
}
