#include <map>
#include <string>

#include <synthrt/Support/DisplayText.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

using srt::DisplayText;
using srt::JsonValue;

namespace {

    DisplayText parse(std::string_view json) {
        auto exp = DisplayText::fromJsonValue(JsonValue::fromJson(json, false));
        BOOST_REQUIRE(exp.hasValue());
        return exp.take();
    }

    srt::Error parseError(std::string_view json) {
        auto exp = DisplayText::fromJsonValue(JsonValue::fromJson(json, false));
        BOOST_REQUIRE(!exp.hasValue());
        return exp.error();
    }

}

BOOST_AUTO_TEST_SUITE(test_DisplayText)

BOOST_AUTO_TEST_CASE(test_DisplayText) {
    {
        auto text = parse(R"(
            {
                "_": "DEF",
                "zh_CN": "CN",
                "zh_TW": "TW"
            }
        )");

        BOOST_CHECK(text.text() == "DEF");
        BOOST_CHECK(text.text("zh_CN") == "CN");
        BOOST_CHECK(text.text("zh_TW") == "TW");
    }

    {
        std::map<std::string, std::string> map;
        map["zh_CN"] = "CN";
        map["zh_TW"] = "TW";

        auto text = DisplayText("DEF", map);

        BOOST_CHECK(text.text() == "DEF");
        BOOST_CHECK(text.text("zh_CN") == "CN");
        BOOST_CHECK(text.text("zh_TW") == "TW");
    }
}

// A bare string is the short form of an object carrying only the default text.
BOOST_AUTO_TEST_CASE(test_DisplayText_StringForm) {
    auto text = parse(R"("DEF")");
    BOOST_CHECK(text.text() == "DEF");
    BOOST_CHECK(text.text("zh_CN") == "DEF");
    BOOST_CHECK(!text.isEmpty());

    // An empty string parses, and counts as empty.
    BOOST_CHECK(parse(R"("")").isEmpty());
}

// An unknown locale falls back to the default rather than returning nothing.
BOOST_AUTO_TEST_CASE(test_DisplayText_LocaleFallback) {
    auto text = parse(R"({"_": "DEF", "zh_CN": "CN"})");

    BOOST_CHECK(text.text("ja_JP") == "DEF");
    BOOST_CHECK(text.text("") == "DEF");
    BOOST_CHECK(text.text("zh") == "DEF"); // no prefix matching, the key must be exact
    BOOST_CHECK(text.text("ZH_CN") == "DEF");

    // The same holds when the object carries no translations at all.
    auto bare = parse(R"({"_": "DEF"})");
    BOOST_CHECK(bare.text() == "DEF");
    BOOST_CHECK(bare.text("zh_CN") == "DEF");

    // A translation may be empty, and that is not the same as being absent.
    auto blank = parse(R"({"_": "DEF", "zh_CN": ""})");
    BOOST_CHECK(blank.text("zh_CN").empty());
    BOOST_CHECK(blank.text("ja_JP") == "DEF");
}

BOOST_AUTO_TEST_CASE(test_DisplayText_Rejects) {
    // Neither a string nor an object.
    BOOST_CHECK(parseError("1").type() == srt::Error::InvalidFormat);
    BOOST_CHECK(parseError("[]").type() == srt::Error::InvalidFormat);
    BOOST_CHECK(parseError("null").type() == srt::Error::InvalidFormat);
    BOOST_CHECK(parseError("true").type() == srt::Error::InvalidFormat);

    // An object without the default entry.
    {
        auto error = parseError(R"({"en_US": "DEF", "zh_CN": "CN"})");
        BOOST_CHECK(error.type() == srt::Error::InvalidFormat);
        BOOST_CHECK(error.message().find(R"("_")") != std::string::npos);
    }
    BOOST_CHECK(parseError("{}").type() == srt::Error::InvalidFormat);

    // The default entry present but not a string.
    BOOST_CHECK(parseError(R"({"_": 1})").type() == srt::Error::InvalidFormat);
    BOOST_CHECK(parseError(R"({"_": null})").type() == srt::Error::InvalidFormat);

    // A translation that is not a string. The message names the offending key.
    {
        auto error = parseError(R"({"_": "DEF", "zh_CN": 1})");
        BOOST_CHECK(error.type() == srt::Error::InvalidFormat);
        BOOST_CHECK(error.message().find("zh_CN") != std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(test_DisplayText_ValueSemantics) {
    // A default-constructed object is empty and answers every locale with nothing.
    {
        DisplayText text;
        BOOST_CHECK(text.isEmpty());
        BOOST_CHECK(text.text().empty());
        BOOST_CHECK(text.text("zh_CN").empty());
    }
    // Constructing from a string sets the default text only.
    //
    // \note The std::string is spelled out because a bare literal is ambiguous between
    //       <tt>DisplayText(std::string)</tt> and the deprecated <tt>DisplayText(const JsonValue &)</tt>,
    //       each one user-defined conversion away.
    {
        DisplayText text(std::string("DEF"));
        BOOST_CHECK(!text.isEmpty());
        BOOST_CHECK(text.text() == "DEF");
        BOOST_CHECK(text.text("zh_CN") == "DEF");
    }
    // Assigning a string replaces the default text and leaves the translations in place.
    {
        std::map<std::string, std::string> map{{"zh_CN", "CN"}};
        DisplayText text("DEF", map);
        text = std::string("OTHER");
        BOOST_CHECK(text.text() == "OTHER");
        BOOST_CHECK(text.text("zh_CN") == "CN");
        BOOST_CHECK(text.text("ja_JP") == "OTHER");
    }
    // swap() exchanges the two.
    {
        DisplayText a{std::string("A")};
        DisplayText b{std::string("B")};
        a.swap(b);
        BOOST_CHECK(a.text() == "B");
        BOOST_CHECK(b.text() == "A");
    }
    // An empty map is accepted, and every locale then falls back.
    {
        DisplayText text("DEF", {});
        BOOST_CHECK(text.text("zh_CN") == "DEF");
    }
}

BOOST_AUTO_TEST_SUITE_END()
