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
        auto text = srt::DisplayText(jsonObj);

        BOOST_VERIFY(text.text() == "DEF");
        BOOST_VERIFY(text.text("zh_CN") == "CN");
        BOOST_VERIFY(text.text("zh_TW") == "TW");
    }

    {
        std::map<std::string, std::string> map;
        map["zh_CN"] = "CN";
        map["zh_TW"] = "TW";

        auto text = srt::DisplayText("DEF", map);

        BOOST_VERIFY(text.text() == "DEF");
        BOOST_VERIFY(text.text("zh_CN") == "CN");
        BOOST_VERIFY(text.text("zh_TW") == "TW");
    }
}

BOOST_AUTO_TEST_CASE(test_DisplayText_fallback) {
    {
        auto json = R"(
            {
                "en_US": "DEF",
                "zh_CN": "CN",
                "zh_TW": "TW"   
            }
        )";
        auto jsonObj = srt::JsonValue::fromJson(json, false);
        auto text = srt::DisplayText(jsonObj);

        BOOST_VERIFY(text.text() == "DEF");
        BOOST_VERIFY(text.text("zh_CN") == "CN");
        BOOST_VERIFY(text.text("zh_TW") == "TW");
    }
}

BOOST_AUTO_TEST_SUITE_END()