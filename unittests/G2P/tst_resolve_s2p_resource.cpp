// LanguageService::resolveS2pResource and S2P cache behavior tests.
//
// Covers:
//   - S2P resource cache identity: same (packageId, singerId, languageId)
//     returns the same shared_ptr on repeated calls.
//   - Different singer / different language produce independent resources.
//   - Invalid inputs (empty packageId, unknown singer) return errors.
//   - Dictionary-mode resource convert() basic functionality.
//
// Note: resolveS2pResource uses the single-version (empty version) route
// internally, so each package is registered with a single version to avoid
// G2pVersionAmbiguous. The S2P cache is per-LanguageService-instance, so
// cache identity tests must use the same LanguageService object.

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/LanguageService.h>
#include <synthrt/G2P/LanguageRoute.h>
#include <synthrt/S2P/LanguageResource.h>

using namespace srt::g2p;

namespace {

    std::filesystem::path makeTempDir(const std::string &name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = std::filesystem::temp_directory_path() /
                   ("srt-g2p-s2p-" + name + "-" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        return dir;
    }

    void writeFile(const std::filesystem::path &path, const std::string &text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << text;
    }

    struct LangSpec {
        std::string id;
        std::string g2pId;
        std::string s2pMode = "dict";
        std::string dictPath;
        std::vector<std::string> g2pPackages;
        std::string g2pPackageVersion;
    };

    void createPackage(const std::filesystem::path &pkgDir,
                       const std::string &packageId,
                       const std::string &version,
                       const std::string &singerId,
                       const std::string &defaultLanguage,
                       const std::vector<LangSpec> &langs) {
        std::string langJson;
        for (size_t i = 0; i < langs.size(); ++i) {
            if (i > 0) langJson += ",\n";
            langJson += "                {\"id\": \"" + langs[i].id + "\", ";
            langJson += "\"g2p\": \"" + langs[i].g2pId + "\", ";
            langJson += "\"s2pMode\": \"" + langs[i].s2pMode + "\"";
            if (!langs[i].dictPath.empty())
                langJson += ", \"dict\": \"" + langs[i].dictPath + "\"";
            if (!langs[i].g2pPackages.empty()) {
                langJson += ", \"g2pPackages\": [";
                for (size_t j = 0; j < langs[i].g2pPackages.size(); ++j) {
                    if (j > 0) langJson += ", ";
                    langJson += "\"" + langs[i].g2pPackages[j] + "\"";
                }
                langJson += "]";
            }
            if (!langs[i].g2pPackageVersion.empty())
                langJson += ", \"g2pPackageVersion\": \"" + langs[i].g2pPackageVersion + "\"";
            langJson += "}";
        }

        std::string desc = "{\n";
        desc += "    \"id\": \"" + packageId + "\",\n";
        desc += "    \"version\": \"" + version + "\",\n";
        desc += "    \"contributes\": {\n";
        desc += "        \"singers\": [\"characters/singer/config.json\"],\n";
        desc += "        \"inferences\": [\"inferences/duration/config.json\"]\n";
        desc += "    }\n";
        desc += "}\n";
        writeFile(pkgDir / "desc.json", desc);

        std::string imports = "{\"inferenceId\": \"duration\"}";
        std::string singer = "{\n";
        singer += "    \"$version\": \"1.0\",\n";
        singer += "    \"id\": \"" + singerId + "\",\n";
        singer += "    \"level\": 1,\n";
        singer += "    \"imports\": [" + imports + "],\n";
        singer += "    \"configuration\": {\n";
        singer += "        \"defaultLanguage\": \"" + defaultLanguage + "\",\n";
        singer += "        \"languages\": [\n" + langJson + "\n        ]\n";
        singer += "    }\n";
        singer += "}\n";
        writeFile(pkgDir / "characters/singer/config.json", singer);

        const auto singerConfigDir = pkgDir / "characters" / "singer";
        for (const auto &lang : langs) {
            for (const auto &g2pPkg : lang.g2pPackages) {
                std::filesystem::create_directories(singerConfigDir / g2pPkg);
            }
        }

        std::string inference = "{\n";
        inference += "    \"id\": \"duration\",\n";
        inference += "    \"class\": \"ai.svs.DurationInference\",\n";
        inference += "    \"level\": 1,\n";
        inference += "    \"configuration\": {}\n";
        inference += "}\n";
        writeFile(pkgDir / "inferences/duration/config.json", inference);
    }

    std::vector<PackageDirectoryEntry> makeEntries(
        const std::vector<std::tuple<std::string, stdc::VersionNumber,
                                     std::filesystem::path>> &items) {
        std::vector<PackageDirectoryEntry> out;
        out.reserve(items.size());
        for (const auto &t : items) {
            out.push_back({std::get<0>(t), std::get<1>(t), std::get<2>(t)});
        }
        return out;
    }

} // namespace

// ===========================================================================
// 1. resolveS2pResource cache hit: calling twice with the same
//    (packageId, singerId, languageId) returns the same shared_ptr (pointer
//    equality), proving the S2P cache is populated on first access.
// ===========================================================================

TEST_CASE("resolveS2pResource cache hit returns same shared_ptr",
          "[g2p][s2p][cache]") {
    const auto root = makeTempDir("cache-hit");
    const std::string pkgA = "s2p.cache.hit";
    const std::string singerId = "cache_hit_singer";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();

    createPackage(root / "pkg_a", pkgA, "1.0.0", singerId, "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
    });
    // PackageParser resolves the dict path relative to the singer config
    // file's parent directory (<pkgDir>/characters/singer/), so the dict
    // file must exist at <pkgDir>/characters/singer/assets/cmn.txt.
    writeFile(root / "pkg_a" / "characters" / "singer" / "assets" / "cmn.txt",
              "ni\tn i\n");

    LanguageService langSvc;
    REQUIRE(langSvc.initializeMetadata(
        {}, {}, makeEntries({{pkgA, v1, root / "pkg_a"}})).hasValue());

    auto res1Exp = langSvc.resolveS2pResource(pkgA, stdc::VersionNumber{}, singerId, "cmn");
    REQUIRE(res1Exp.hasValue());
    REQUIRE(*res1Exp != nullptr);

    auto res2Exp = langSvc.resolveS2pResource(pkgA, stdc::VersionNumber{}, singerId, "cmn");
    REQUIRE(res2Exp.hasValue());
    REQUIRE(*res2Exp != nullptr);

    // Cache hit: same underlying pointer.
    REQUIRE(res1Exp->get() == res2Exp->get());

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 2. resolveS2pResource different singer produces a different resource:
//    two packages with different singers get independent cache entries.
// ===========================================================================

TEST_CASE("resolveS2pResource different singer different resource",
          "[g2p][s2p][cache]") {
    const auto root = makeTempDir("diff-singer");
    const std::string pkgA = "s2p.diff.singer.a";
    const std::string pkgB = "s2p.diff.singer.b";
    const std::string singerA = "diff_singer_a";
    const std::string singerB = "diff_singer_b";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();

    createPackage(root / "pkg_a", pkgA, "1.0.0", singerA, "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
    });
    createPackage(root / "pkg_b", pkgB, "1.0.0", singerB, "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
    });
    // PackageParser resolves dict paths relative to <pkgDir>/characters/singer/.
    writeFile(root / "pkg_a" / "characters" / "singer" / "assets" / "cmn.txt",
              "ni\tn i\n");
    writeFile(root / "pkg_b" / "characters" / "singer" / "assets" / "cmn.txt",
              "ni\tn i\n");

    LanguageService langSvc;
    REQUIRE(langSvc.initializeMetadata(
        {}, {},
        makeEntries({{pkgA, v1, root / "pkg_a"},
                     {pkgB, v1, root / "pkg_b"}})).hasValue());

    auto resAExp = langSvc.resolveS2pResource(pkgA, stdc::VersionNumber{}, singerA, "cmn");
    auto resBExp = langSvc.resolveS2pResource(pkgB, stdc::VersionNumber{}, singerB, "cmn");
    REQUIRE(resAExp.hasValue());
    REQUIRE(resBExp.hasValue());
    REQUIRE(*resAExp != nullptr);
    REQUIRE(*resBExp != nullptr);

    // Different (packageId, singerId) tuples → different cache slots →
    // different underlying pointers.
    REQUIRE(resAExp->get() != resBExp->get());

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 3. resolveS2pResource different language produces a different resource:
//    one package with two languages (cmn, en) gets independent cache entries.
// ===========================================================================

TEST_CASE("resolveS2pResource different language different resource",
          "[g2p][s2p][cache]") {
    const auto root = makeTempDir("diff-lang");
    const std::string pkgA = "s2p.diff.lang";
    const std::string singerId = "diff_lang_singer";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();

    createPackage(root / "pkg_a", pkgA, "1.0.0", singerId, "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
        {"en", "g2p-en-official", "dict", "assets/en.txt", {}, ""},
    });
    // PackageParser resolves dict paths relative to <pkgDir>/characters/singer/.
    writeFile(root / "pkg_a" / "characters" / "singer" / "assets" / "cmn.txt",
              "ni\tn i\n");
    writeFile(root / "pkg_a" / "characters" / "singer" / "assets" / "en.txt",
              "hello\th ah l ow\n");

    LanguageService langSvc;
    REQUIRE(langSvc.initializeMetadata(
        {}, {}, makeEntries({{pkgA, v1, root / "pkg_a"}})).hasValue());

    auto resCmnExp = langSvc.resolveS2pResource(pkgA, stdc::VersionNumber{}, singerId, "cmn");
    auto resEnExp = langSvc.resolveS2pResource(pkgA, stdc::VersionNumber{}, singerId, "en");
    REQUIRE(resCmnExp.hasValue());
    REQUIRE(resEnExp.hasValue());
    REQUIRE(*resCmnExp != nullptr);
    REQUIRE(*resEnExp != nullptr);

    // Same (packageId, singerId) but different languageId → different cache
    // slots → different underlying pointers.
    REQUIRE(resCmnExp->get() != resEnExp->get());

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 4. resolveS2pResource invalid inputs:
//    - Empty packageId → route resolution returns G2pPackageNotFound (no
//      matching package directory).
//    - Unknown singer → route resolution returns G2pRouteNotFound.
// ===========================================================================

TEST_CASE("resolveS2pResource empty packageId returns error",
          "[g2p][s2p][error]") {
    LanguageService langSvc;

    auto resExp = langSvc.resolveS2pResource("", stdc::VersionNumber{}, "singer_x", "cmn");
    REQUIRE(!resExp.hasValue());
    REQUIRE(resExp.takeError().code() ==
            srt::core::ErrorCode::G2pPackageNotFound);
}

TEST_CASE("resolveS2pResource unknown singer returns G2pRouteNotFound",
          "[g2p][s2p][error]") {
    const auto root = makeTempDir("unknown-singer");
    const std::string pkgA = "s2p.unknown.singer";
    const std::string singerId = "known_singer";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();

    createPackage(root / "pkg_a", pkgA, "1.0.0", singerId, "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
    });
    // PackageParser resolves dict paths relative to <pkgDir>/characters/singer/.
    writeFile(root / "pkg_a" / "characters" / "singer" / "assets" / "cmn.txt",
              "ni\tn i\n");

    LanguageService langSvc;
    REQUIRE(langSvc.initializeMetadata(
        {}, {}, makeEntries({{pkgA, v1, root / "pkg_a"}})).hasValue());

    auto resExp = langSvc.resolveS2pResource(pkgA, stdc::VersionNumber{}, "missing_singer", "cmn");
    REQUIRE(!resExp.hasValue());
    REQUIRE(resExp.takeError().code() ==
            srt::core::ErrorCode::G2pRouteNotFound);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 5. S2P dict mode basic conversion: create a dict file with a known
//    entry, resolve the resource, and verify convert() returns the expected
//    phoneme sequence.
// ===========================================================================

TEST_CASE("resolveS2pResource dict mode convert returns expected phonemes",
          "[g2p][s2p][convert]") {
    const auto root = makeTempDir("dict-convert");
    const std::string pkgA = "s2p.dict.convert";
    const std::string singerId = "dict_convert_singer";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();

    createPackage(root / "pkg_a", pkgA, "1.0.0", singerId, "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
    });
    // DictionaryS2P format: "pronunciation\tphoneme1 phoneme2 ..." per line.
    // PackageParser resolves dict paths relative to <pkgDir>/characters/singer/.
    writeFile(root / "pkg_a" / "characters" / "singer" / "assets" / "cmn.txt",
              "ni\tn i\nhao\th ao\n");

    LanguageService langSvc;
    REQUIRE(langSvc.initializeMetadata(
        {}, {}, makeEntries({{pkgA, v1, root / "pkg_a"}})).hasValue());

    auto resExp = langSvc.resolveS2pResource(pkgA, stdc::VersionNumber{}, singerId, "cmn");
    REQUIRE(resExp.hasValue());
    REQUIRE(*resExp != nullptr);

    const auto &resource = **resExp;
    auto syllable = resource.convert("ni");
    REQUIRE(syllable.phonemes.size() == 2);
    REQUIRE(syllable.phonemes[0] == "n");
    REQUIRE(syllable.phonemes[1] == "i");

    auto syllable2 = resource.convert("hao");
    REQUIRE(syllable2.phonemes.size() == 2);
    REQUIRE(syllable2.phonemes[0] == "h");
    REQUIRE(syllable2.phonemes[1] == "ao");

    // Unknown pronunciation returns an empty phoneme vector (dictionary miss
    // is not an error at the S2P layer; the caller decides how to handle it).
    auto syllableMiss = resource.convert("unknown_word");
    REQUIRE(syllableMiss.phonemes.empty());

    std::filesystem::remove_all(root);
}
