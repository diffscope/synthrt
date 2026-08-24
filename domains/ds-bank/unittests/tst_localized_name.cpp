// Localized singer/language/speaker name tests (ds-spec 2.4 多语言文本).
//
// Manifests keep the FULL translation map as srt::core::DisplayText; no locale
// is threaded through the parser/scanner, and the Runtime performs NO matching
// at all:
//   - text() returns the "_" default entry;
//   - text(key) is an exact, case-sensitive key lookup (nullptr when absent) —
//     no Lookup, no case folding, no fallback;
//   - keys are opaque: POSIX-style "zh_CN" is an ordinary key like any other;
//   - locales() enumerates every key except "_" verbatim;
//   - objects without "_" select a default text by the legacy resolution order
//     ("default" -> "en" -> first entry) and still keep every translation.
// How a caller maps a UI language onto these keys (candidate list, fallback,
// case merge) is entirely the front-end's decision and out of scope here.

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

    std::string getOr(const srt::core::DisplayText &text, std::string_view key,
                      std::string fallback) {
        if (const auto *value = text.text(key)) {
            return *value;
        }
        return fallback;
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

TEST_CASE("Parser keeps full translations; the Runtime performs no matching",
          "[ds-bank][localized-name]") {
    const auto dir = makeLocalizedPackage("display-text-full");

    // Parse ONCE. Every key is served verbatim from the same manifest.
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
    CHECK(langName.text() == "Mandarin");
    CHECK(spkName.text() == "Internal Emb");

    // Exact key lookup.
    CHECK(getOr(singerName, "zh-Hans", "<null>") == "君凝华");
    CHECK(getOr(singerName, "zh-Hant", "<null>") == "君凝華");
    CHECK(getOr(singerName, "ja", "<null>") == "うろこ音凝華");
    CHECK(getOr(langName, "zh-CN", "<null>") == "普通话");
    CHECK(getOr(langName, "zh-TW", "<null>") == "普通話");
    CHECK(getOr(spkName, "zh-CN", "<null>") == "内置音色");

    // No matching rules: no subtag stripping, no case folding, no fallback.
    CHECK(singerName.text("zh-Hans-CN") == nullptr);
    CHECK(singerName.text("zh-Hant-HK") == nullptr);
    CHECK(singerName.text("ZH-hANS") == nullptr);
    CHECK(langName.text("ja-JP-x-u-ca-japanese") == nullptr);
    CHECK(singerName.text("zh-TW") == nullptr);
    CHECK(singerName.text("zh") == nullptr);
    CHECK(singerName.text("fr") == nullptr);

    // locales(): every key except "_", verbatim (ja/zh-Hans/zh-Hant sorted).
    const auto keys = singerName.locales();
    REQUIRE(keys.size() == 3);
    CHECK(keys[0] == "ja");
    CHECK(keys[1] == "zh-Hans");
    CHECK(keys[2] == "zh-Hant");

    std::filesystem::remove_all(dir);
}

TEST_CASE("POSIX-style keys are ordinary opaque keys",
          "[ds-bank][localized-name]") {
    const auto dir = makeTempPackageDir("posix-keys-ordinary");
    writeFile(dir / "desc.json", R"json({"id":"pkg","version":"1.0.0","contributes":{"singers":["characters/demo/config.json"]}})json");
    writeFile(dir / "characters/demo/config.json", R"json({"id":"demo","name":{"_":"Default","zh_CN":"普通话","zh_TW":"普通話"}})json");

    PackageParser parser;
    auto parsed = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
    REQUIRE(parsed.hasValue());
    const auto &name = parsed.value().singers().front().name();

    // The translations are retained verbatim and fetchable by exact spelling.
    CHECK(name.locales().size() == 2);
    CHECK(getOr(name, "zh_CN", "<null>") == "普通话");
    CHECK(getOr(name, "zh_TW", "<null>") == "普通話");
    CHECK(name.text() == "Default");

    // No separator normalization: "zh-CN" is simply a different key.
    CHECK(name.text("zh-CN") == nullptr);
    // Case-sensitive: "ZH_cn" does not hit "zh_CN".
    CHECK(name.text("ZH_cn") == nullptr);

    std::filesystem::remove_all(dir);
}

TEST_CASE("Plain-string and legacy default/en names keep working",
          "[ds-bank][localized-name]") {
    {
        // Plain string name: default text only, no keys.
        const auto dir = makeTempPackageDir("plain-name");
        writeFile(dir / "desc.json", R"json({"id":"pkg","version":"1.0.0","contributes":{"singers":["characters/demo/config.json"]}})json");
        writeFile(dir / "characters/demo/config.json", R"json({"id":"demo","name":"Solo Name"})json");

        PackageParser parser;
        auto parsed = parser.parsePackage(dir, PackageParser::ParseMode::Relaxed);
        REQUIRE(parsed.hasValue());
        const auto &name = parsed.value().singers().front().name();
        CHECK(name.text() == "Solo Name");
        CHECK(name.text("zh-CN") == nullptr);
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
        CHECK(getOr(name, "zh-Hans", "<null>") == "中文名");
        CHECK(getOr(name, "en", "<null>") == "English Name");
        // No matching: "en-US" does NOT resolve to the "en" entry.
        CHECK(name.text("en-US") == nullptr);
        CHECK(name.locales().size() == 2);

        std::filesystem::remove_all(dir);
    }
}

TEST_CASE("Scanner snapshots carry full translations (single scan serves all keys)",
          "[ds-bank][localized-name]") {
    const auto dir = makeLocalizedPackage("scanner-display-text");

    ds::bank::VoicebankScanner scanner;
    scanner.setSearchPaths({dir});
    auto packages = scanner.refresh();
    REQUIRE(packages.hasValue());
    const auto &singers = scanner.singers();
    REQUIRE(singers.size() == 1);
    const auto &name = singers.front().name;

    // One scan; the host resolves any key set from the cached snapshot.
    CHECK(name.text() == "Jun Ninghua");
    CHECK(getOr(name, "zh-Hans", "<null>") == "君凝华");
    CHECK(getOr(name, "zh-Hant", "<null>") == "君凝華");
    CHECK(getOr(name, "ja", "<null>") == "うろこ音凝華");
    CHECK(name.text("fr") == nullptr); // no fallback: the host decides to use text()

    std::filesystem::remove_all(dir);
}
