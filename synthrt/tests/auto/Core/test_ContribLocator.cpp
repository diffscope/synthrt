#include <synthrt/Core/ContribLocator.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

using srt::ContribLocator;

BOOST_AUTO_TEST_SUITE(test_ContribLocator)

BOOST_AUTO_TEST_CASE(test_parse_and_render) {
    const auto local = ContribLocator::fromString(":singer/main");
    BOOST_REQUIRE(local.isValid());
    BOOST_CHECK(local.isLocal());
    BOOST_CHECK_EQUAL(local.category(), "singer");
    BOOST_CHECK_EQUAL(local.contributionId(), "main");
    BOOST_CHECK_EQUAL(local.toString(), ":singer/main");

    const auto external = ContribLocator::fromString("vendor/sample:com.example.category/item-1");
    BOOST_REQUIRE(external.isValid());
    BOOST_CHECK_EQUAL(external.packageId(), "vendor/sample");
    BOOST_CHECK_EQUAL(external.category(), "com.example.category");
    BOOST_CHECK_EQUAL(external.contributionId(), "item-1");
    BOOST_CHECK_EQUAL(external.toString(), "vendor/sample:com.example.category/item-1");
}

BOOST_AUTO_TEST_CASE(test_rejects_incomplete_and_non_ascii_locators) {
    for (const auto value : {"", "singer/main", ":singer", ":/main", ":singer/",
                              ":singer/main/extra", "foo::singer/main", ":singer/声音"}) {
        BOOST_CHECK_MESSAGE(!ContribLocator::fromString(value).isValid(), value);
    }
}

BOOST_AUTO_TEST_SUITE_END()
