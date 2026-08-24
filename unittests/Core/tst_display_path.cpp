// Unit tests for srt::core::DisplayPath (multi-language path pass-through).
//
// Same contract as DisplayText (ds-spec 2.4): the Runtime performs no
// matching — path(key) is an exact, case-sensitive key lookup returning
// nullptr for absent keys, and locales() exposes every key except "_".

#include <map>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Support/DisplayPath.h>

using srt::core::DisplayPath;
using srt::core::Error;
using srt::core::JsonValue;

namespace {

    DisplayPath parse(std::string_view json) {
        auto exp = DisplayPath::fromJsonValue(JsonValue::fromJson(json, false));
        REQUIRE(exp.hasValue());
        return exp.take();
    }

} // namespace

TEST_CASE("DisplayPath pass-through semantics", "[core][displaypath]") {
    auto paths = parse(R"({"_": "readme.txt", "zh-CN": "readme.zh-CN.txt", "zh_CN": "posix.txt"})");

    CHECK(paths.path() == "readme.txt");

    // Exact keys hit; POSIX-style keys are ordinary opaque keys.
    REQUIRE(paths.path("zh-CN") != nullptr);
    CHECK(*paths.path("zh-CN") == "readme.zh-CN.txt");
    REQUIRE(paths.path("zh_CN") != nullptr);
    CHECK(*paths.path("zh_CN") == "posix.txt");

    // No Lookup, no case folding, no fallback.
    CHECK(paths.path("zh-CN-extra") == nullptr);
    CHECK(paths.path("ZH-cn") == nullptr);
    CHECK(paths.path("ja-JP") == nullptr);

    // locales(): every key except "_", sorted.
    const auto locales = paths.locales();
    REQUIRE(locales.size() == 2);
    CHECK(locales[0] == "zh-CN");
    CHECK(locales[1] == "zh_CN");

    // String short form: default path only.
    auto bare = parse(R"("readme.txt")");
    CHECK(bare.path() == "readme.txt");
    CHECK(bare.path("zh-CN") == nullptr);
    CHECK(bare.locales().empty());

    // Strict parsing still requires the "_" entry in object form.
    auto exp = DisplayPath::fromJsonValue(JsonValue::fromJson(R"({"en": "a.txt"})", false));
    CHECK(!exp.hasValue());
    CHECK(exp.error().type() == Error::InvalidFormat);
}

TEST_CASE("DisplayPath constructor ignores '_' map entry", "[core][displaypath]") {
    DisplayPath guarded("def.txt", {{"_", "shadow.txt"}, {"en", "en.txt"}});
    CHECK(guarded.path() == "def.txt");
    CHECK(guarded.path("_") == nullptr);
    REQUIRE(guarded.locales().size() == 1);
    CHECK(guarded.locales()[0] == "en");
}
