// Localized singer/language/speaker name resolution tests.
//
// PackageParser::nameField() picks a display name from a `name` object map:
//   - no locale (legacy): prefer "default"/"en", else first key in sorted order;
//   - with locale: normalized exact match -> language main-code match (with
//     Simp/Trad script preference) -> "_" fallback -> first key.
// VoicebankScanner/VoicebankSession thread the locale through to the parser.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <stdcorelib/path.h>

#include <diffsinger/Bank/PackageParser.h>
#include <diffsinger/Bank/VoicebankScanner.h>

namespace {
    std::filesystem::path makeTempPackageDir(const std::string &name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = std::filesystem::temp_directory_path() /
                   ("ds-bank-locale-" + name + "-" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        return dir;
    }

    void writeFile(const std::filesystem::path &path, const std::string &text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << text;
    }
} // namespace

using ds::bank::PackageParser;

// Opens the OpenUtau-style localized `name` object (dash-style keys kept in
// their declared order; JSON object is stored sorted by key).
static const char *kLocalizedNameJson =
    R"json({"id":"Junninghua","name":{"_":"Jun Ninghua","ja":"うろこ音凝華","zh-Hans":"君凝华","zh-Hant":"君凝華"},"configuration":{"defaultLanguage":"cmn","languages":[{"id":"cmn","g2p":"g2p-cmn-official","name":{"_":"Mandarin","ja_JP":"中国語","zh_CN":"普通话","zh_TW":"普通話"}}],"speakers":[{"id":"internal_emb","name":{"_":"Internal Emb","zh_CN":"内置音色"}}]}})json";

std::filesystem::path makeLocalizedPackage(const std::string &name) {
    const auto dir = makeTempPackageDir(name);
    writeFile(dir / "desc.json", R"json({"id":"junninghua","version":"1.0.0","contributes":{"singers":["characters/Junninghua/config.json"]}})json");
    writeFile(dir / "characters/Junninghua/config.json", kLocalizedNameJson);
    return dir;
}

TEST_CASE("Parser resolves singer name object per parser display locale (OpenUtau style)",
          "[ds-bank][localized-name]") {
    const auto dir = makeLocalizedPackage("singer-locale");

    {
        // Legacy: no locale → existing behavior (prefer default/en, else first
        // key which sorted is "_").
        PackageParser parser;
        auto parsed = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
        REQUIRE(parsed.hasValue());
        REQUIRE(parsed.value().singers().size() == 1);
        CHECK(parsed.value().singers().front().name() == "Jun Ninghua");
        const auto &lang = parsed.value().singers().front().languages().front();
        CHECK(lang.name() == "Mandarin");
    }

    {
        // zh_CN → zh-Hans value "君凝华" and language "普通话".
        PackageParser parser;
        parser.setDisplayLocale("zh_CN");
        auto parsed = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
        REQUIRE(parsed.hasValue());
        const auto &singer = parsed.value().singers().front();
        CHECK(singer.name() == "君凝华");
        CHECK(singer.languages().front().name() == "普通话");
        CHECK(singer.speakers().front().name() == "内置音色");
    }

    {
        // zh_TW → zh-Hant value "君凝華".
        PackageParser parser;
        parser.setDisplayLocale("zh_TW");
        auto parsed = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
        REQUIRE(parsed.hasValue());
        CHECK(parsed.value().singers().front().name() == "君凝華");
    }

    {
        // ja → exact key "ja" → "うろこ音凝華".
        PackageParser parser;
        parser.setDisplayLocale("ja");
        auto parsed = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
        REQUIRE(parsed.hasValue());
        CHECK(parsed.value().singers().front().name() == "うろこ音凝華");
    }

    {
        // en_US / unsupported locale → "_" fallback.
        PackageParser parser;
        parser.setDisplayLocale("en_US");
        auto parsed = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
        REQUIRE(parsed.hasValue());
        CHECK(parsed.value().singers().front().name() == "Jun Ninghua");
    }

    std::filesystem::remove_all(dir);
}

TEST_CASE("Parser handles underscore-style keys and plain-string names",
          "[ds-bank][localized-name]") {
    {
        // Underscore-style keys (QLocale form) resolve too.
        const auto dir = makeTempPackageDir("uk-locale");
        writeFile(dir / "desc.json", R"json({"id":"pkg","version":"1.0.0","contributes":{"singers":["characters/demo/config.json"]}})json");
        writeFile(dir / "characters/demo/config.json", R"json({"id":"demo","name":{"_":"Default","ja_JP":"日本語","zh_CN":"普通话","zh_TW":"普通話"}})json");

        {
            PackageParser parser;
            auto parsed = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
            REQUIRE(parsed.hasValue());
            CHECK(parsed.value().singers().front().name() == "Default");
        }
        {
            PackageParser parser;
            parser.setDisplayLocale("zh_CN");
            auto parsed = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
            REQUIRE(parsed.hasValue());
            CHECK(parsed.value().singers().front().name() == "普通话");
        }
        {
            PackageParser parser;
            parser.setDisplayLocale("zh_TW");
            auto parsed = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
            REQUIRE(parsed.hasValue());
            CHECK(parsed.value().singers().front().name() == "普通話");
        }
        {
            PackageParser parser;
            parser.setDisplayLocale("ja");
            auto parsed = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
            REQUIRE(parsed.hasValue());
            CHECK(parsed.value().singers().front().name() == "日本語");
        }

        std::filesystem::remove_all(dir);
    }

    {
        // Plain string name is used verbatim regardless of locale.
        const auto dir = makeTempPackageDir("plain-name");
        writeFile(dir / "desc.json", R"json({"id":"pkg","version":"1.0.0","contributes":{"singers":["characters/demo/config.json"]}})json");
        writeFile(dir / "characters/demo/config.json", R"json({"id":"demo","name":"Solo Name"})json");

        PackageParser parser;
        parser.setDisplayLocale("zh_CN");
        auto parsed = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
        REQUIRE(parsed.hasValue());
        CHECK(parsed.value().singers().front().name() == "Solo Name");

        std::filesystem::remove_all(dir);
    }
}

TEST_CASE("VoicebankScanner threads display locale into singer snapshots",
          "[ds-bank][localized-name]") {
    const auto dir = makeLocalizedPackage("scanner-locale");

    {
        ds::bank::VoicebankScanner scanner;
        scanner.setSearchPaths({dir});
        auto packages = scanner.refresh();
        REQUIRE(packages.hasValue());
        const auto &singers = scanner.singers();
        REQUIRE(singers.size() == 1);
        CHECK(singers.front().name == "Jun Ninghua");
    }

    {
        ds::bank::VoicebankScanner scanner;
        scanner.setSearchPaths({dir});
        scanner.setDisplayLocale("zh_CN");
        auto packages = scanner.refresh();
        REQUIRE(packages.hasValue());
        const auto &singers = scanner.singers();
        REQUIRE(singers.size() == 1);
        CHECK(singers.front().name == "君凝华");
    }

    {
        ds::bank::VoicebankScanner scanner;
        scanner.setSearchPaths({dir});
        scanner.setDisplayLocale("zh_TW");
        auto packages = scanner.refresh();
        REQUIRE(packages.hasValue());
        REQUIRE(!scanner.singers().empty());
        CHECK(scanner.singers().front().name == "君凝華");
    }

    std::filesystem::remove_all(dir);
}
