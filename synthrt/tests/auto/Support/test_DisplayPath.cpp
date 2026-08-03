#include <filesystem>
#include <map>
#include <string>

#include <stdcorelib/path.h>

#include <synthrt/Support/DisplayPath.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace fs = std::filesystem;

using srt::DisplayPath;
using srt::JsonValue;

namespace {

    DisplayPath parse(std::string_view json) {
        auto exp = DisplayPath::fromJsonValue(JsonValue::fromJson(json, false));
        BOOST_REQUIRE(exp.hasValue());
        return exp.take();
    }

    srt::Error parseError(std::string_view json) {
        auto exp = DisplayPath::fromJsonValue(JsonValue::fromJson(json, false));
        BOOST_REQUIRE(!exp.hasValue());
        return exp.error();
    }

    /// Comparisons go through this rather than through \c fs::path 's own narrow constructor, which
    /// reads the ANSI code page on Windows and would disagree with what \c fromJsonValue() built.
    fs::path p(std::string_view utf8) {
        return stdc::path::from_utf8(utf8);
    }

}

BOOST_AUTO_TEST_SUITE(test_DisplayPath)

BOOST_AUTO_TEST_CASE(test_DisplayPath) {
    {
        auto path = parse(R"(
            {
                "_": "assets/avatar.png",
                "zh_CN": "assets/avatar_zh.png",
                "ja_JP": "assets/avatar_ja.png"
            }
        )");

        BOOST_CHECK(path.path() == p("assets/avatar.png"));
        BOOST_CHECK(path.path("zh_CN") == p("assets/avatar_zh.png"));
        BOOST_CHECK(path.path("ja_JP") == p("assets/avatar_ja.png"));
    }

    {
        std::map<std::string, fs::path> map;
        map["zh_CN"] = p("assets/avatar_zh.png");
        map["ja_JP"] = p("assets/avatar_ja.png");

        DisplayPath path(p("assets/avatar.png"), map);

        BOOST_CHECK(path.path() == p("assets/avatar.png"));
        BOOST_CHECK(path.path("zh_CN") == p("assets/avatar_zh.png"));
        BOOST_CHECK(path.path("ja_JP") == p("assets/avatar_ja.png"));
    }
}

// A bare string is the short form of an object carrying only the default path.
BOOST_AUTO_TEST_CASE(test_DisplayPath_StringForm) {
    auto path = parse(R"("assets/avatar.png")");
    BOOST_CHECK(path.path() == p("assets/avatar.png"));
    BOOST_CHECK(path.path("zh_CN") == p("assets/avatar.png"));
    BOOST_CHECK(!path.isEmpty());

    // An empty string parses, and counts as empty.
    BOOST_CHECK(parse(R"("")").isEmpty());
}

// An unknown locale falls back to the default rather than returning nothing.
BOOST_AUTO_TEST_CASE(test_DisplayPath_LocaleFallback) {
    auto path = parse(R"({"_": "a.png", "zh_CN": "b.png"})");

    BOOST_CHECK(path.path("ja_JP") == p("a.png"));
    BOOST_CHECK(path.path("") == p("a.png"));
    BOOST_CHECK(path.path("zh") == p("a.png")); // no prefix matching, the key must be exact
    BOOST_CHECK(path.path("ZH_CN") == p("a.png"));

    // The same holds when the object carries no translations at all.
    auto bare = parse(R"({"_": "a.png"})");
    BOOST_CHECK(bare.path() == p("a.png"));
    BOOST_CHECK(bare.path("zh_CN") == p("a.png"));

    // A translation may be empty, and that is not the same as being absent.
    auto blank = parse(R"({"_": "a.png", "zh_CN": ""})");
    BOOST_CHECK(blank.path("zh_CN").empty());
    BOOST_CHECK(blank.path("ja_JP") == p("a.png"));
}

BOOST_AUTO_TEST_CASE(test_DisplayPath_Rejects) {
    // Neither a string nor an object.
    BOOST_CHECK(parseError("1").type() == srt::Error::InvalidFormat);
    BOOST_CHECK(parseError("[]").type() == srt::Error::InvalidFormat);
    BOOST_CHECK(parseError("null").type() == srt::Error::InvalidFormat);
    BOOST_CHECK(parseError("true").type() == srt::Error::InvalidFormat);

    // An object without the default entry.
    {
        auto error = parseError(R"({"zh_CN": "a.png"})");
        BOOST_CHECK(error.type() == srt::Error::InvalidFormat);
        BOOST_CHECK(error.message().find(R"("_")") != std::string::npos);
    }
    BOOST_CHECK(parseError("{}").type() == srt::Error::InvalidFormat);

    // The default entry present but not a string.
    BOOST_CHECK(parseError(R"({"_": 1})").type() == srt::Error::InvalidFormat);
    BOOST_CHECK(parseError(R"({"_": null})").type() == srt::Error::InvalidFormat);

    // A translation that is not a string. The message names the offending key.
    {
        auto error = parseError(R"({"_": "a.png", "zh_CN": 1})");
        BOOST_CHECK(error.type() == srt::Error::InvalidFormat);
        BOOST_CHECK(error.message().find("zh_CN") != std::string::npos);
    }
}

// What the manifest wrote is what comes back out.
//
// \note \c DisplayPath holds no base directory, so nothing here is resolved against the package or
//       singer directory. That join is the caller's to make, against \c PackageRef::path() or
//       \c SingerSpec::path(). Manifests in the wild do write relative paths, \c ../assets/avatar.png
//       among them, so a value taken straight from here is not openable on its own.
BOOST_AUTO_TEST_CASE(test_DisplayPath_KeptVerbatim) {
    // A relative path stays relative.
    {
        auto path = parse(R"("../assets/avatar.png")");
        BOOST_CHECK(path.path().is_relative());
        BOOST_CHECK(path.path() == p("../assets/avatar.png"));
    }
    // The lexical parts survive untouched - no cleaning, no canonicalization.
    {
        auto path = parse(R"("a/./b/../c.png")");
        BOOST_CHECK(path.path() == p("a/./b/../c.png"));
        BOOST_CHECK(path.path() != p("a/c.png"));
    }
    // Forward slashes are not rewritten to the platform's separator.
    {
        auto path = parse(R"("a/b/c.png")");
        BOOST_CHECK(stdc::path::to_utf8(path.path()) == "a/b/c.png");
    }
    // An absolute path is left alone as well.
    {
        auto path = parse(R"("/opt/share/avatar.png")");
        BOOST_CHECK(path.path() == p("/opt/share/avatar.png"));
    }
}

// Decoding is the reason DisplayPath exists as its own type rather than a DisplayText that callers
// convert. A manifest is UTF-8, while std::filesystem::path reads a narrow string in the ANSI code
// page on Windows, so anything outside it would be mangled on the way in.
BOOST_AUTO_TEST_CASE(test_DisplayPath_Utf8) {
    // "歌手/头像.png", spelled in bytes so the test does not depend on this file's own encoding.
    const std::string utf8 = "\xE6\xAD\x8C\xE6\x89\x8B/\xE5\xA4\xB4\xE5\x83\x8F.png";

    auto path = parse("\"" + utf8 + "\"");
    BOOST_CHECK(stdc::path::to_utf8(path.path()) == utf8);
    BOOST_CHECK(path.path() == stdc::path::from_utf8(utf8));

    // And through the object form, for both the default and a translation.
    auto localized = parse(R"({"_": "a.png", "zh_CN": ")" + utf8 + R"("})");
    BOOST_CHECK(stdc::path::to_utf8(localized.path("zh_CN")) == utf8);
    BOOST_CHECK(localized.path() == p("a.png"));
}

BOOST_AUTO_TEST_CASE(test_DisplayPath_ValueSemantics) {
    // A default-constructed object is empty and answers every locale with nothing.
    {
        DisplayPath path;
        BOOST_CHECK(path.isEmpty());
        BOOST_CHECK(path.path().empty());
        BOOST_CHECK(path.path("zh_CN").empty());
    }
    // Constructing from a path sets the default only.
    {
        DisplayPath path(p("a.png"));
        BOOST_CHECK(!path.isEmpty());
        BOOST_CHECK(path.path() == p("a.png"));
        BOOST_CHECK(path.path("zh_CN") == p("a.png"));
    }
    // Assigning a path replaces the default and leaves the translations in place.
    {
        std::map<std::string, fs::path> map{{"zh_CN", p("b.png")}};
        DisplayPath path(p("a.png"), map);
        path = p("c.png");
        BOOST_CHECK(path.path() == p("c.png"));
        BOOST_CHECK(path.path("zh_CN") == p("b.png"));
        BOOST_CHECK(path.path("ja_JP") == p("c.png"));
    }
    // swap() exchanges the two.
    {
        DisplayPath a(p("a.png"));
        DisplayPath b(p("b.png"));
        a.swap(b);
        BOOST_CHECK(a.path() == p("b.png"));
        BOOST_CHECK(b.path() == p("a.png"));
    }
    // An empty map is accepted, and every locale then falls back.
    {
        DisplayPath path(p("a.png"), {});
        BOOST_CHECK(path.path("zh_CN") == p("a.png"));
    }
}

BOOST_AUTO_TEST_SUITE_END()
