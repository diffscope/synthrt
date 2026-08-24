// Unit tests for srt::core::DisplayText.
//
// Ported from main's synthrt/tests/auto/Support/test_DisplayText.cpp
// (Boost.Test -> Catch2) and extended for the ds-spec 2.4 localization rules:
//   - object form requires the "_" default entry (strict parsing);
//   - the Runtime performs NO matching: text(key) is an exact, case-sensitive
//     key lookup returning nullptr for absent keys, with no Lookup, no case
//     folding and no fallback to the default text;
//   - keys are opaque: POSIX-style "zh_CN" is an ordinary key like any other;
//   - locales() exposes every key except "_" verbatim, in sorted order;
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

    // Exact key lookup, unwrapped for readable assertions.
    const std::string *get(const DisplayText &text, std::string_view key) {
        return text.text(key);
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
        REQUIRE(get(text, "zh-CN") != nullptr);
        CHECK(*get(text, "zh-CN") == "CN");
        REQUIRE(get(text, "zh-TW") != nullptr);
        CHECK(*get(text, "zh-TW") == "TW");
    }

    {
        std::map<std::string, std::string> map;
        map["zh-CN"] = "CN";
        map["zh-TW"] = "TW";

        auto text = DisplayText("DEF", map);

        CHECK(text.text() == "DEF");
        REQUIRE(get(text, "zh-CN") != nullptr);
        CHECK(*get(text, "zh-CN") == "CN");
        REQUIRE(get(text, "zh-TW") != nullptr);
        CHECK(*get(text, "zh-TW") == "TW");
    }
}

// A bare string is the short form of an object carrying only the default text.
TEST_CASE("DisplayText string form", "[core][displaytext]") {
    auto text = parse(R"("DEF")");
    CHECK(text.text() == "DEF");
    CHECK(get(text, "zh-CN") == nullptr);
    CHECK(!text.isEmpty());

    // An empty string parses, and counts as empty.
    CHECK(parse(R"("")").isEmpty());
}

// The Runtime does no matching (ds-spec 2.4): text(key) is an exact key
// lookup, case-sensitive, without subtag stripping or default fallback.
TEST_CASE("DisplayText exact key lookup, no matching rules", "[core][displaytext]") {
    auto text = parse(R"({"_": "DEF", "zh-Hans": "HANS", "zh": "ZH", "ja-JP": "JP"})");

    // Exact keys hit.
    REQUIRE(get(text, "ja-JP") != nullptr);
    CHECK(*get(text, "ja-JP") == "JP");

    // No Lookup: a longer preference does NOT strip its way to a stored key.
    CHECK(get(text, "zh-Hans-CN") == nullptr);
    CHECK(get(text, "zh-Hant-TW") == nullptr);
    // ...and a shorter preference does NOT reach a longer stored key either.
    CHECK(get(text, "ja") == nullptr);

    // Matching is case-sensitive: keys are opaque byte strings.
    CHECK(get(text, "JA-jp") == nullptr);
    CHECK(get(text, "ZH-hANS-cn") == nullptr);

    // Unknown keys yield nothing; the caller decides when to use text().
    CHECK(get(text, "ko") == nullptr);
    CHECK(get(text, "ko-KR") == nullptr);
    CHECK(get(text, "") == nullptr);
    CHECK(text.text() == "DEF");
}

// POSIX-style keys are ordinary opaque keys: retained verbatim and fetchable
// by their exact spelling, just not by any other spelling.
TEST_CASE("DisplayText treats POSIX-style keys as ordinary keys", "[core][displaytext]") {
    auto posix = parse(R"({"_": "DEF", "zh_CN": "POSIX"})");

    REQUIRE(posix.locales().size() == 1);
    CHECK(posix.locales()[0] == "zh_CN");
    REQUIRE(get(posix, "zh_CN") != nullptr);
    CHECK(*get(posix, "zh_CN") == "POSIX");
    // No separator normalization: "zh-CN" is a different opaque key.
    CHECK(get(posix, "zh-CN") == nullptr);
    CHECK(get(posix, "ZH_cn") == nullptr);
}

TEST_CASE("DisplayText absent keys versus empty values", "[core][displaytext]") {
    auto text = parse(R"({"_": "DEF", "zh-CN": "CN"})");

    CHECK(get(text, "ja-JP") == nullptr);

    // The same holds when the object carries no translations at all.
    auto bare = parse(R"({"_": "DEF"})");
    CHECK(bare.text() == "DEF");
    CHECK(get(bare, "zh-CN") == nullptr);
    CHECK(bare.locales().empty());

    // A translation may be empty, and that is not the same as being absent:
    // the caller must be able to tell the two apart (pointer, not reference).
    auto blank = parse(R"({"_": "DEF", "zh-CN": ""})");
    REQUIRE(get(blank, "zh-CN") != nullptr);
    CHECK(get(blank, "zh-CN")->empty());
    CHECK(get(blank, "ja-JP") == nullptr);
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
    // A default-constructed object is empty and holds no keys.
    {
        DisplayText text;
        CHECK(text.isEmpty());
        CHECK(text.text().empty());
        CHECK(get(text, "zh-CN") == nullptr);
    }
    // Constructing from a string sets the default text only.
    {
        DisplayText text("DEF");
        CHECK(!text.isEmpty());
        CHECK(text.text() == "DEF");
        CHECK(get(text, "zh-CN") == nullptr);
    }
    // Assigning a string replaces the default text and leaves the translations in place.
    {
        std::map<std::string, std::string> map{{"zh-CN", "CN"}};
        DisplayText text("DEF", map);
        text = std::string("OTHER");
        CHECK(text.text() == "OTHER");
        REQUIRE(get(text, "zh-CN") != nullptr);
        CHECK(*get(text, "zh-CN") == "CN");
        CHECK(get(text, "ja-JP") == nullptr);
    }
    // swap() exchanges the two.
    {
        DisplayText a("A");
        DisplayText b("B");
        a.swap(b);
        CHECK(a.text() == "B");
        CHECK(b.text() == "A");
    }
    // An empty map is accepted.
    {
        DisplayText text("DEF", {});
        CHECK(get(text, "zh-CN") == nullptr);
        CHECK(text.locales().empty());
    }
}

// locales() exposes exactly the translated tags (never "_"), verbatim, in
// sorted order.
TEST_CASE("DisplayText locales()", "[core][displaytext]") {
    auto text = parse(R"({"_": "DEF", "zh-TW": "TW", "zh-CN": "CN", "ja-JP": "JP"})");
    const auto locales = text.locales();
    REQUIRE(locales.size() == 3);
    CHECK(locales[0] == "ja-JP");
    CHECK(locales[1] == "zh-CN");
    CHECK(locales[2] == "zh-TW");

    CHECK(parse(R"("DEF")").locales().empty());

    // A "_" entry passed through the map constructor is not a translation key:
    // the default text comes from the default-text argument only.
    DisplayText guarded("DEF", {{"_", "SHADOW"}, {"en", "EN"}});
    CHECK(guarded.text() == "DEF");
    CHECK(get(guarded, "_") == nullptr);
    REQUIRE(guarded.locales().size() == 1);
    CHECK(guarded.locales()[0] == "en");
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
    REQUIRE(noDefault.text("zh-CN") != nullptr);
    CHECK(*noDefault.text("zh-CN") == "CN");
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
    REQUIRE(mixed.text("zh-CN") != nullptr);
    CHECK(*mixed.text("zh-CN") == "CN");
    CHECK(mixed.locales().size() == 1);
}
