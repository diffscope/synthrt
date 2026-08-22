// Unit tests for srt::core::DisplayText.
//
// Ported from main's synthrt/tests/auto/Support/test_DisplayText.cpp
// (Boost.Test -> Catch2) and extended for the ds-spec 2.4 localization rules:
//   - object form requires the "_" default entry (strict parsing);
//   - text(locale) is RFC 4647 Lookup (strip-from-right), case-insensitive;
//   - the '-' separator is strict: POSIX-style "zh_CN" keys never match a
//     BCP 47 lookup (and vice versa);
//   - fromJsonValueTolerant() never fails and reproduces the legacy
//     "default" -> "en" -> first-entry default selection.

#include <map>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Support/DisplayText.h>

using srt::core::DisplayText;
using srt::core::Error;
using srt::core::JsonValue;

namespace {

    DisplayText parse(std::string_view json) {
        auto exp = DisplayText::fromJsonValue(JsonValue::fromJson(json, false));
        REQUIRE(exp.hasValue());
        return exp.take();
    }

    Error parseError(std::string_view json) {
        auto exp = DisplayText::fromJsonValue(JsonValue::fromJson(json, false));
        REQUIRE(!exp.hasValue());
        return exp.error();
    }

} // namespace

TEST_CASE("DisplayText basic object form", "[core][displaytext]") {
    {
        auto text = parse(R"(
            {
                "_": "DEF",
                "zh-CN": "CN",
                "zh-TW": "TW"
            }
        )");

        CHECK(text.text() == "DEF");
        CHECK(text.text("zh-CN") == "CN");
        CHECK(text.text("zh-TW") == "TW");
    }

    {
        std::map<std::string, std::string> map;
        map["zh-CN"] = "CN";
        map["zh-TW"] = "TW";

        auto text = DisplayText("DEF", map);

        CHECK(text.text() == "DEF");
        CHECK(text.text("zh-CN") == "CN");
        CHECK(text.text("zh-TW") == "TW");
    }
}

// A bare string is the short form of an object carrying only the default text.
TEST_CASE("DisplayText string form", "[core][displaytext]") {
    auto text = parse(R"("DEF")");
    CHECK(text.text() == "DEF");
    CHECK(text.text("zh-CN") == "DEF");
    CHECK(!text.isEmpty());

    // An empty string parses, and counts as empty.
    CHECK(parse(R"("")").isEmpty());
}

// RFC 4647 Lookup: strip from the right until a match, else the default.
TEST_CASE("DisplayText lookup matching (ds-spec 2.4)", "[core][displaytext]") {
    auto text = parse(R"({"_": "DEF", "zh-Hans": "HANS", "zh": "ZH", "ja-JP": "JP"})");

    // Exact match wins.
    CHECK(text.text("ja-JP") == "JP");
    // Strip-from-right: zh-Hans-CN -> zh-Hans.
    CHECK(text.text("zh-Hans-CN") == "HANS");
    CHECK(text.text("zh-Hans-SG") == "HANS");
    // Two-step strip: zh-Hant-TW -> zh-Hant (absent) -> zh.
    CHECK(text.text("zh-Hant-TW") == "ZH");
    // No script inference: zh-TW does NOT reach zh-Hans; it lands on zh.
    CHECK(text.text("zh-TW") == "ZH");
    // Lookup is case-insensitive on both exact and stripped forms.
    CHECK(text.text("JA-jp") == "JP");
    CHECK(text.text("ZH-hANS-cn") == "HANS");
    // Unknown language falls back to the default.
    CHECK(text.text("ko") == "DEF");
    CHECK(text.text("ko-KR") == "DEF");
    CHECK(text.text("") == "DEF");

    // No cross-separator matching: POSIX-style keys are inert.
    auto posix = parse(R"({"_": "DEF", "zh_CN": "POSIX"})");
    CHECK(posix.text("zh-CN") == "DEF");
    CHECK(posix.text("zh_CN") == "DEF"); // "zh_CN" is not a '-'-separated tag
}

// An unknown locale falls back to the default rather than returning nothing.
TEST_CASE("DisplayText locale fallback", "[core][displaytext]") {
    auto text = parse(R"({"_": "DEF", "zh-CN": "CN"})");

    CHECK(text.text("ja-JP") == "DEF");
    CHECK(text.text("") == "DEF");

    // The same holds when the object carries no translations at all.
    auto bare = parse(R"({"_": "DEF"})");
    CHECK(bare.text() == "DEF");
    CHECK(bare.text("zh-CN") == "DEF");

    // A translation may be empty, and that is not the same as being absent.
    auto blank = parse(R"({"_": "DEF", "zh-CN": ""})");
    CHECK(blank.text("zh-CN").empty());
    CHECK(blank.text("ja-JP") == "DEF");
}

TEST_CASE("DisplayText rejects invalid JSON shapes", "[core][displaytext]") {
    // Neither a string nor an object.
    CHECK(parseError("1").type() == Error::InvalidFormat);
    CHECK(parseError("[]").type() == Error::InvalidFormat);
    CHECK(parseError("null").type() == Error::InvalidFormat);
    CHECK(parseError("true").type() == Error::InvalidFormat);

    // An object without the default entry.
    {
        auto error = parseError(R"({"en-US": "DEF", "zh-CN": "CN"})");
        CHECK(error.type() == Error::InvalidFormat);
        CHECK(error.message().find(R"("_")") != std::string::npos);
    }
    CHECK(parseError("{}").type() == Error::InvalidFormat);

    // The default entry present but not a string.
    CHECK(parseError(R"({"_": 1})").type() == Error::InvalidFormat);
    CHECK(parseError(R"({"_": null})").type() == Error::InvalidFormat);

    // A translation that is not a string. The message names the offending key.
    {
        auto error = parseError(R"({"_": "DEF", "zh-CN": 1})");
        CHECK(error.type() == Error::InvalidFormat);
        CHECK(error.message().find("zh-CN") != std::string::npos);
    }
}

TEST_CASE("DisplayText value semantics", "[core][displaytext]") {
    // A default-constructed object is empty and answers every locale with nothing.
    {
        DisplayText text;
        CHECK(text.isEmpty());
        CHECK(text.text().empty());
        CHECK(text.text("zh-CN").empty());
    }
    // Constructing from a string sets the default text only.
    {
        DisplayText text("DEF");
        CHECK(!text.isEmpty());
        CHECK(text.text() == "DEF");
        CHECK(text.text("zh-CN") == "DEF");
    }
    // Assigning a string replaces the default text and leaves the translations in place.
    {
        std::map<std::string, std::string> map{{"zh-CN", "CN"}};
        DisplayText text("DEF", map);
        text = std::string("OTHER");
        CHECK(text.text() == "OTHER");
        CHECK(text.text("zh-CN") == "CN");
        CHECK(text.text("ja-JP") == "OTHER");
    }
    // swap() exchanges the two.
    {
        DisplayText a("A");
        DisplayText b("B");
        a.swap(b);
        CHECK(a.text() == "B");
        CHECK(b.text() == "A");
    }
    // An empty map is accepted, and every locale then falls back.
    {
        DisplayText text("DEF", {});
        CHECK(text.text("zh-CN") == "DEF");
    }
}

// locales() exposes exactly the translated tags, in sorted order.
TEST_CASE("DisplayText locales()", "[core][displaytext]") {
    auto text = parse(R"({"_": "DEF", "zh-TW": "TW", "zh-CN": "CN", "ja-JP": "JP"})");
    const auto locales = text.locales();
    REQUIRE(locales.size() == 3);
    CHECK(locales[0] == "ja-JP");
    CHECK(locales[1] == "zh-CN");
    CHECK(locales[2] == "zh-TW");

    CHECK(parse(R"("DEF")").locales().empty());
}

TEST_CASE("DisplayText tolerant parsing", "[core][displaytext]") {
    // String form identical to strict parsing.
    CHECK(DisplayText::fromJsonValueTolerant(JsonValue::fromJson(R"("DEF")", false)).text() ==
          "DEF");

    // Non-string/non-object yields an empty text instead of an error.
    CHECK(DisplayText::fromJsonValueTolerant(JsonValue::fromJson("1", false)).isEmpty());
    CHECK(DisplayText::fromJsonValueTolerant(JsonValue::fromJson("[]", false)).isEmpty());

    // Full translations are preserved even when "_" is missing...
    auto noDefault =
        DisplayText::fromJsonValueTolerant(JsonValue::fromJson(R"({"en": "EN", "zh-CN": "CN"})", false));
    CHECK(noDefault.text() == "EN"); // "en" selected as the default text
    CHECK(noDefault.text("zh-CN") == "CN");
    CHECK(noDefault.locales().size() == 2); // ...including the key that fed the default

    // Legacy "default" key beats "en".
    auto legacyOrder = DisplayText::fromJsonValueTolerant(
        JsonValue::fromJson(R"({"default": "D", "en": "EN"})", false));
    CHECK(legacyOrder.text() == "D");

    // Otherwise the first string entry (sorted key order) is the default.
    auto firstKey = DisplayText::fromJsonValueTolerant(
        JsonValue::fromJson(R"({"yue": "YUE", "zh-CN": "CN"})", false));
    CHECK(firstKey.text() == "YUE");

    // Non-string entries are skipped rather than rejected.
    auto mixed = DisplayText::fromJsonValueTolerant(
        JsonValue::fromJson(R"({"_": "DEF", "zh-CN": "CN", "ja-JP": 1})", false));
    CHECK(mixed.text() == "DEF");
    CHECK(mixed.text("zh-CN") == "CN");
    CHECK(mixed.locales().size() == 1);
}
