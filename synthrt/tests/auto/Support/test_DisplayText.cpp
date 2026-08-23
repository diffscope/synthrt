#include <map>
#include <string>

#include <synthrt/Support/DisplayText.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(test_DisplayText)

BOOST_AUTO_TEST_CASE(test_preserves_the_complete_language_map) {
    const std::map<std::string, std::string> translations = {
        {"ZH-cn", "upper"},
        {"zh-CN", "lower"},
    };
    srt::DisplayText text("default", translations);

    BOOST_CHECK_EQUAL(text.text(), "default");
    BOOST_CHECK_EQUAL(text.text("ZH-cn"), "upper");
    BOOST_CHECK_EQUAL(text.text("zh-CN"), "lower");
    BOOST_CHECK_EQUAL(text.text("unknown"), "default");
    BOOST_REQUIRE_EQUAL(text.locales().size(), 2u);
    BOOST_CHECK_EQUAL(text.locales()[0], "ZH-cn");
    BOOST_CHECK_EQUAL(text.locales()[1], "zh-CN");
}

BOOST_AUTO_TEST_CASE(test_value_semantics) {
    srt::DisplayText empty;
    BOOST_CHECK(empty.isEmpty());

    srt::DisplayText first("first");
    srt::DisplayText second = first;
    BOOST_CHECK_EQUAL(second.text(), "first");

    srt::DisplayText replacement("replacement");
    first.swap(replacement);
    BOOST_CHECK_EQUAL(first.text(), "replacement");
    BOOST_CHECK_EQUAL(replacement.text(), "first");
    BOOST_CHECK_EQUAL(second.text(), "first");
}

BOOST_AUTO_TEST_SUITE_END()
