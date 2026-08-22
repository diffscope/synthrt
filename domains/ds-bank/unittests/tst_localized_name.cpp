// Localized singer/language/speaker name tests (ds-spec 2.4 多语言文本).
//
// Manifests keep the FULL translation map as srt::core::DisplayText; no locale
// is threaded through the parser/scanner. Callers resolve with text(locale):
//   - BCP 47 tags, RFC 4647 Lookup (strip-from-right), case-insensitive;
//   - POSIX-style "zh_CN" keys are inert (strict '-' separator);
//   - objects without "_" select a default text by the legacy resolution order
//     ("default" -> "en" -> first entry) and still keep every translation.

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

// OpenUtau-style localized `name` objects, BCP 47 keys.
static const char *kLocalizedNameJson =
    R"json({"id":"Junninghua","name":{"_":"Jun Ninghua","ja":"うろこ音凝華","zh-Hans":"君凝华","zh-Hant":"君凝華"},"configuration":{"defaultLanguage":"cmn","languages":[{"id":"cmn","g2p":"g2p-cmn-official","name":{"_":"Mandarin","ja-JP":"中国語","zh-CN":"普通话","zh-TW":"普通話"}}],"speakers":[{"id":"internal_emb","name":{"_":"Internal Emb","zh-CN":"内置音色"}}]}})json";

std::filesystem::path makeLocalizedPackage(const std::string &name) {
    const auto dir = makeTempPackageDir(name);
    writeFile(dir / "desc.json", R"json({"id":"junninghua","version":"1.0.0","contributes":{"singers":["characters/Junninghua/config.json"]}})json");
    writeFile(dir / "characters/Junninghua/config.json", kLocalizedNameJson);
    return dir;
}

TEST_CASE("Parser keeps full translations; resolution happens at text(locale)",
          "[ds-bank][localized-name]") {
    const auto dir = makeLocalizedPackage("display-text-full");

    // Parse ONCE. Every locale is served from the same manifest.
    PackageParser parser;
    auto parsed = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    REQUIRE(parsed.hasValue());
    REQUIRE(parsed.value().singers().size() == 1);
    const auto &singer = parsed.value().singers().front();
    const auto &singerName = singer.name();
    const auto &langName = singer.languages().front().name();
    const auto &spkName = singer.speakers().front().name();

    // Default ("_") text.
    CHECK(singerName.text() == "Jun Ninghua");
    CHECK(singerName.text("") == "Jun Ninghua");
    CHECK(langName.text() == "Mandarin");
    CHECK(spkName.text() == "Internal Emb");

    // Exact BCP 47 keys.
    CHECK(singerName.text("zh-Hans") == "君凝华");
    CHECK(singerName.text("zh-Hant") == "君凝華");
    CHECK(singerName.text("ja") == "うろこ音凝華");
    CHECK(langName.text("zh-CN") == "普通话");
    CHECK(langName.text("zh-TW") == "普通話");
    CHECK(spkName.text("zh-CN") == "内置音色");

    // RFC 4647 Lookup: strip the rightmost subtag of the preference.
    CHECK(singerName.text("zh-Hans-CN") == "君凝华");   // zh-Hans-CN -> zh-Hans
    CHECK(singerName.text("zh-Hant-HK") == "君凝華");   // zh-Hant-HK -> zh-Hant
    CHECK(langName.text("ja-JP-x-u-ca-japanese") == "中国語"); // ... -> ja-JP
    // Lookup is case-insensitive.
    CHECK(singerName.text("ZH-hANS") == "君凝华");
    std::filesystem::remove_all(dir);
}

TEST_CASE("Lookup falls back without script inference (spec-conformant)",
          "[ds-bank][localized-name]") {
    const auto dir = makeLocalizedPackage("lookup-no-script-guess");

    PackageParser parser;
    auto parsed = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    REQUIRE(parsed.hasValue());
    const auto &name = parsed.value().singers().front().name();

    // Keys are zh-Hans / zh-Hant only. A zh-TW preference tries zh-TW, then zh
    // (absent), then "_": NO Hans/Hant guessing (ds-spec 2.4 RFC 4647 Lookup).
    CHECK(name.text("zh-TW") == "Jun Ninghua");
    CHECK(name.text("zh") == "Jun Ninghua");

    std::filesystem::remove_all(dir);
}

TEST_CASE("POSIX-style keys are inert under strict BCP 47 matching",
          "[ds-bank][localized-name]") {
    const auto dir = makeTempPackageDir("posix-keys-inert");
    writeFile(dir / "desc.json", R"json({"id":"pkg","version":"1.0.0","contributes":{"singers":["characters/demo/config.json"]}})json");
    writeFile(dir / "characters/demo/config.json", R"json({"id":"demo","name":{"_":"Default","zh_CN":"普通话","zh_TW":"普通話"}})json");

    PackageParser parser;
    auto parsed = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    REQUIRE(parsed.hasValue());
    const auto &name = parsed.value().singers().front().name();

    // The translations are retained verbatim (visible via locales())...
    CHECK(name.locales().size() == 2);
    // ...but never match a BCP 47 preference: zh_CN is not zh-CN.
    CHECK(name.text("zh-CN") == "Default");
    CHECK(name.text("zh_CN") == "Default");
    CHECK(name.text() == "Default");

    std::filesystem::remove_all(dir);
}

TEST_CASE("Plain-string and legacy default/en names keep working",
          "[ds-bank][localized-name]") {
    {
        // Plain string name is used verbatim regardless of locale.
        const auto dir = makeTempPackageDir("plain-name");
        writeFile(dir / "desc.json", R"json({"id":"pkg","version":"1.0.0","contributes":{"singers":["characters/demo/config.json"]}})json");
        writeFile(dir / "characters/demo/config.json", R"json({"id":"demo","name":"Solo Name"})json");

        PackageParser parser;
        auto parsed = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
        REQUIRE(parsed.hasValue());
        const auto &name = parsed.value().singers().front().name();
        CHECK(name.text() == "Solo Name");
        CHECK(name.text("zh-CN") == "Solo Name");
        CHECK(name.locales().empty());

        std::filesystem::remove_all(dir);
    }
    {
        // No "_" entry: legacy default selection ("default"/"en"/first), all
        // translations retained.
        const auto dir = makeTempPackageDir("legacy-no-default");
        writeFile(dir / "desc.json", R"json({"id":"pkg","version":"1.0.0","contributes":{"singers":["characters/demo/config.json"]}})json");
        writeFile(dir / "characters/demo/config.json", R"json({"id":"demo","name":{"en":"English Name","zh-Hans":"中文名"}})json");

        PackageParser parser;
        auto parsed = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
        REQUIRE(parsed.hasValue());
        const auto &name = parsed.value().singers().front().name();
        CHECK(name.text() == "English Name");
        CHECK(name.text("zh-Hans") == "中文名");
        CHECK(name.text("en-US") == "English Name"); // en-US -> en via Lookup
        CHECK(name.locales().size() == 2);

        std::filesystem::remove_all(dir);
    }
}

TEST_CASE("Scanner snapshots carry full translations (single scan serves all locales)",
          "[ds-bank][localized-name]") {
    const auto dir = makeLocalizedPackage("scanner-display-text");

    ds::bank::VoicebankScanner scanner;
    scanner.setSearchPaths({dir});
    auto packages = scanner.refresh();
    REQUIRE(packages.hasValue());
    const auto &singers = scanner.singers();
    REQUIRE(singers.size() == 1);
    const auto &name = singers.front().name;

    // One scan; every UI language resolves from the cached snapshot.
    CHECK(name.text() == "Jun Ninghua");
    CHECK(name.text("zh-Hans") == "君凝华");
    CHECK(name.text("zh-Hant") == "君凝華");
    CHECK(name.text("ja") == "うろこ音凝華");
    CHECK(name.text("fr") == "Jun Ninghua"); // unsupported -> default

    std::filesystem::remove_all(dir);
}
