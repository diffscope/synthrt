#include <synthrt/Support/DisplayText.h>

#include <boost/test/unit_test.hpp>

using namespace stdc;

BOOST_AUTO_TEST_SUITE(test_DisplayText)

BOOST_AUTO_TEST_CASE(test_DisplayText) {
    {
        auto json = R"(
            {
                "_": "DEF",
                "zh_CN": "CN",
                "zh_TW": "TW"   
            }
        )";
        auto jsonObj = srt::JsonValue::fromJson(json, false);
        auto exp = srt::DisplayText::fromJsonValue(jsonObj);
        BOOST_REQUIRE(exp.hasValue());
        auto text = exp.take();

        BOOST_CHECK(text.text() == "DEF");
        BOOST_CHECK(text.text("zh_CN") == "CN");
        BOOST_CHECK(text.text("zh_TW") == "TW");
    }

    {
        std::map<std::string, std::string> map;
        map["zh_CN"] = "CN";
        map["zh_TW"] = "TW";

        auto text = srt::DisplayText("DEF", map);

        BOOST_CHECK(text.text() == "DEF");
        BOOST_CHECK(text.text("zh_CN") == "CN");
        BOOST_CHECK(text.text("zh_TW") == "TW");
    }
}

BOOST_AUTO_TEST_CASE(test_DisplayText_missing_default) {
    {
        auto json = R"(
            {
                "en_US": "DEF",
                "zh_CN": "CN",
                "zh_TW": "TW"   
            }
        )";
        auto jsonObj = srt::JsonValue::fromJson(json, false);
        auto exp = srt::DisplayText::fromJsonValue(jsonObj);
        BOOST_CHECK(!exp.hasValue());
    }
}

BOOST_AUTO_TEST_SUITE_END()
