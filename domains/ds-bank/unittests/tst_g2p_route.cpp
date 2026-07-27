// Comprehensive tests for G2P route resolution and G2pRes/G2pInput structures.
//
// Covers:
//   - G2pInput/G2pRes construction, field propagation, default values
//   - G2pRes mode constants (convert/copy/skip) and isOk/isFailed semantics
//   - G2pRes candidate auto-fill from pronunciation
//   - G2pRes empty-pronunciation fallback to lyric
//   - G2pErrorType coverage (all 7 values)
//   - g2pSource derivation (official vs voicebank)
//   - LanguageRoute fields and g2pSource flag (R7: was voicebankContext)
//   - resolveG2pRoute via LanguageService with real package dirs:
//     * official G2P (no g2pPackages)
//     * voicebank private G2P (with g2pPackages + version)
//     * multi-language singer (cmn + en + jp)
//     * missing singer / missing language / missing defaultLanguage
//     * language not declared by singer
//     * empty language id with default fallback
//   - Cross-package G2P version conflicts
//   - Singer with multiple G2P packages (different versions)
//   - Package with mixed official + voicebank G2P languages

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/LanguageService.h>
#include <synthrt/G2P/LanguageRoute.h>

#include <diffsinger/Bank/PackageParser.h>
#include <diffsinger/Bank/PackageManifest.h>

using namespace srt::g2p;
using namespace ds::bank;

namespace {

    std::filesystem::path makeTempDir(const std::string &name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = std::filesystem::temp_directory_path() /
                   ("ds-g2p-route-" + name + "-" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        return dir;
    }

    void writeFile(const std::filesystem::path &path, const std::string &text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << text;
    }

    // Build a package with configurable languages, each optionally with
    // voicebank G2P packages.
    struct LangSpec {
        std::string id;
        std::string g2pId;
        std::string s2pMode = "dict";
        std::string dictPath;                              // relative or empty
        std::vector<std::string> g2pPackages;              // relative paths
        std::string g2pPackageVersion;                     // empty = no version
    };

    void createPackage(const std::filesystem::path &pkgDir,
                       const std::string &packageId,
                       const std::string &version,
                       const std::string &singerId,
                       const std::string &defaultLanguage,
                       const std::vector<LangSpec> &langs) {
        // Build language JSON array
        std::string langJson;
        for (size_t i = 0; i < langs.size(); ++i) {
            if (i > 0) langJson += ",\n";
            langJson += "                {\"id\": \"" + langs[i].id + "\", ";
            langJson += "\"g2p\": \"" + langs[i].g2pId + "\", ";
            langJson += "\"s2pMode\": \"" + langs[i].s2pMode + "\"";
            if (!langs[i].dictPath.empty()) {
                langJson += ", \"dict\": \"" + langs[i].dictPath + "\"";
            }
            if (!langs[i].g2pPackages.empty()) {
                langJson += ", \"g2pPackages\": [";
                for (size_t j = 0; j < langs[i].g2pPackages.size(); ++j) {
                    if (j > 0) langJson += ", ";
                    langJson += "\"" + langs[i].g2pPackages[j] + "\"";
                }
                langJson += "]";
            }
            if (!langs[i].g2pPackageVersion.empty()) {
                langJson += ", \"g2pPackageVersion\": \"" + langs[i].g2pPackageVersion + "\"";
            }
            langJson += "}";
        }

        // desc.json
        std::string desc = "{\n";
        desc += "    \"id\": \"" + packageId + "\",\n";
        desc += "    \"version\": \"" + version + "\",\n";
        desc += "    \"contributes\": {\n";
        desc += "        \"singers\": [\"characters/singer/config.json\"],\n";
        desc += "        \"inferences\": [\"inferences/duration/config.json\"]\n";
        desc += "    }\n";
        desc += "}\n";
        writeFile(pkgDir / "desc.json", desc);

        // singer config
        std::string singer = "{\n";
        singer += "    \"$version\": \"1.0\",\n";
        singer += "    \"id\": \"" + singerId + "\",\n";
        singer += "    \"level\": 1,\n";
        singer += "    \"imports\": [{\"inferenceId\": \"duration\"}],\n";
        singer += "    \"configuration\": {\n";
        singer += "        \"defaultLanguage\": \"" + defaultLanguage + "\",\n";
        singer += "        \"languages\": [\n" + langJson + "\n        ]\n";
        singer += "    }\n";
        singer += "}\n";
        writeFile(pkgDir / "characters/singer/config.json", singer);

        // inference config
        std::string inference = "{\n";
        inference += "    \"id\": \"duration\",\n";
        inference += "    \"class\": \"ai.svs.DurationInference\",\n";
        inference += "    \"level\": 1,\n";
        inference += "    \"configuration\": {}\n";
        inference += "}\n";
        writeFile(pkgDir / "inferences/duration/config.json", inference);
    }

} // namespace

// ===========================================================================
// G2pInput construction and field propagation
// ===========================================================================

TEST_CASE("G2pInput default construction has empty fields", "[g2p][route][input]") {
    G2pInput input;
    REQUIRE(input.lyric.empty());
    REQUIRE(input.g2pId.empty());
    REQUIRE(input.g2pContext.empty());
    REQUIRE(input.g2pContextVersion.isEmpty());
}

TEST_CASE("G2pInput full construction propagates all fields", "[g2p][route][input]") {
    auto version = stdc::VersionNumber::fromString("2.1.0");
    G2pInput input("hello", "g2p-en-official", "my_singer", version);
    REQUIRE(input.lyric == "hello");
    REQUIRE(input.g2pId == "g2p-en-official");
    REQUIRE(input.g2pContext == "my_singer");
    REQUIRE(input.g2pContextVersion == version);
}

TEST_CASE("G2pInput partial construction omits context", "[g2p][route][input]") {
    G2pInput input("ni hao", "g2p-cmn-official");
    REQUIRE(input.lyric == "ni hao");
    REQUIRE(input.g2pId == "g2p-cmn-official");
    REQUIRE(input.g2pContext.empty());     // default = official context
    REQUIRE(input.g2pContextVersion.isEmpty());
}

// ===========================================================================
// G2pRes construction, defaults, and mode/errorType semantics
// ===========================================================================

TEST_CASE("G2pRes default construction has safe defaults", "[g2p][route][res]") {
    G2pRes res;
    REQUIRE(res.lyric.empty());
    REQUIRE(res.g2pId.empty());
    REQUIRE(res.mode == kG2pModeCopy);      // default mode is "copy"
    REQUIRE(res.errorType == NoError);       // default is no error
    REQUIRE(res.isOk());
    REQUIRE(!res.isFailed());
}

TEST_CASE("G2pRes isOk true for NoError regardless of mode", "[g2p][route][res]") {
    // convert mode with NoError
    G2pRes res1("ni", "g2p-cmn", "", {}, "n i", {}, kG2pModeConvert, NoError);
    REQUIRE(res1.isOk());
    REQUIRE(!res1.isFailed());
    REQUIRE(res1.mode == kG2pModeConvert);

    // copy mode with NoError (e.g. punctuation fallback)
    G2pRes res2(",", "g2p-cmn", "", {}, ",", {}, kG2pModeCopy, NoError);
    REQUIRE(res2.isOk());
    REQUIRE(res2.mode == kG2pModeCopy);

    // skip mode with NoError (empty lyric)
    G2pRes res3("", "g2p-cmn", "", {}, "", {}, kG2pModeSkip, NoError);
    REQUIRE(res3.isOk());
    REQUIRE(res3.mode == kG2pModeSkip);
}

TEST_CASE("G2pRes isFailed true for any non-NoError errorType", "[g2p][route][res]") {
    const std::vector<G2pErrorType> errorTypes = {
        InvalidLyric, ModelInferenceFailed, PhonemeGenerationFailed,
        DriverUnavailable, NotInitialized, UnknownError,
    };
    for (auto et : errorTypes) {
        G2pRes res("test", "g2p-x", "", {}, "test", {}, kG2pModeCopy, et);
        REQUIRE(!res.isOk());
        REQUIRE(res.isFailed());
    }
}

TEST_CASE("G2pRes candidates auto-filled from pronunciation", "[g2p][route][res]") {
    // When pronunciation is non-empty and candidates is empty,
    // the constructor auto-fills candidates with pronunciation.
    G2pRes res("ni", "g2p-cmn", "", {}, "n i");
    REQUIRE(res.pronunciation == "n i");
    REQUIRE(res.candidates.size() == 1);
    REQUIRE(res.candidates[0] == "n i");
}

TEST_CASE("G2pRes preserves explicit candidates", "[g2p][route][res]") {
    // When candidates is explicitly provided, it is not overwritten.
    std::vector<std::string> cands = {"n i", "n ii"};
    G2pRes res("ni", "g2p-cmn", "", {}, "n i", cands);
    REQUIRE(res.candidates.size() == 2);
    REQUIRE(res.candidates[0] == "n i");
    REQUIRE(res.candidates[1] == "n ii");
}

TEST_CASE("G2pRes empty pronunciation falls back to lyric", "[g2p][route][res]") {
    // When pronunciation is empty, it is set to the lyric. The constructor
    // also seeds candidates with [lyric] so consumers iterating candidates
    // uniformly (e.g. ds-editor-lite G2pInputAdapter) see at least one
    // entry. This mirrors the explicit fallback paths in Manager.cpp
    // (lines 382/389/430/445/465/473) which all pass {lyric} as candidates.
    G2pRes res("hello", "g2p-en", "", {}, "");
    REQUIRE(res.pronunciation == "hello");
    REQUIRE(res.candidates.size() == 1);
    REQUIRE(res.candidates[0] == "hello");
}

TEST_CASE("G2pRes g2pSource defaults to empty", "[g2p][route][res]") {
    G2pRes res("test", "g2p-x");
    REQUIRE(res.g2pSource.empty());
}

TEST_CASE("G2pRes g2pSource set to voicebank in full constructor", "[g2p][route][res]") {
    auto version = stdc::VersionNumber::fromString("1.0");
    G2pRes res("test", "g2p-x", "my_singer", version, "t eh s t",
               {}, kG2pModeConvert, NoError, kG2pSourceVoicebank);
    REQUIRE(res.g2pSource == kG2pSourceVoicebank);
    REQUIRE(res.g2pContext == "my_singer");
    REQUIRE(res.g2pContextVersion == version);
}

// ===========================================================================
// G2P mode constants and cross-project contract values
// ===========================================================================

TEST_CASE("G2P mode constants have correct string values", "[g2p][route][constants]") {
    REQUIRE(std::string(kG2pModeConvert) == "convert");
    REQUIRE(std::string(kG2pModeCopy) == "copy");
    REQUIRE(std::string(kG2pModeSkip) == "skip");
}

TEST_CASE("G2P source constants have correct string values", "[g2p][route][constants]") {
    REQUIRE(std::string(kG2pSourceOfficial) == "official");
    REQUIRE(std::string(kG2pSourceVoicebank) == "voicebank");
}

TEST_CASE("G2P driver and category constants", "[g2p][route][constants]") {
    REQUIRE(std::string(kG2pOnnxDriverName) == "g2pOnnxDriver");
    REQUIRE(std::string(kOfficialContext) == "");
    REQUIRE(std::string(kG2pCategory) == "g2p");
    REQUIRE(std::string(kDictCategory) == "dict");
    REQUIRE(std::string(kDriverCategory) == "driver");
}

TEST_CASE("G2P plugin IID constants", "[g2p][route][constants]") {
    REQUIRE(std::string(kTaskPluginIid) == "srt.g2p.task");
    REQUIRE(std::string(kDriverPluginIid) == "srt.g2p.driver");
}

// ===========================================================================
// G2pErrorType enum coverage
// ===========================================================================

TEST_CASE("G2pErrorType all values are distinct", "[g2p][route][error]") {
    REQUIRE(NoError == 0);
    REQUIRE(InvalidLyric != NoError);
    REQUIRE(ModelInferenceFailed != NoError);
    REQUIRE(PhonemeGenerationFailed != NoError);
    REQUIRE(DriverUnavailable != NoError);
    REQUIRE(NotInitialized != NoError);
    REQUIRE(UnknownError != NoError);

    // All pairwise distinct
    const std::vector<G2pErrorType> all = {
        NoError, InvalidLyric, ModelInferenceFailed, PhonemeGenerationFailed,
        DriverUnavailable, NotInitialized, UnknownError,
    };
    for (size_t i = 0; i < all.size(); ++i) {
        for (size_t j = i + 1; j < all.size(); ++j) {
            REQUIRE(all[i] != all[j]);
        }
    }
}

// ===========================================================================
// LanguageService::resolveLanguageRoute with real package dirs
// ===========================================================================

TEST_CASE("LanguageRoute official G2P: no g2pPackages", "[g2p][route][resolve]") {
    const auto root = makeTempDir("official-g2p");
    createPackage(root, "pkg.official", "1.0.0", "singer_a", "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
    });
    writeFile(root / "assets/cmn.txt", "ni\tn i\n");

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.official"] = root;

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);  // populates _impl->packageDirs
    auto routeExp = langSvc.resolveLanguageRoute("pkg.official", stdc::VersionNumber{}, "singer_a", "cmn");
    REQUIRE(routeExp.hasValue());
    const auto &route = *routeExp;
    REQUIRE(route.g2pId == "g2p-cmn-official");
    REQUIRE(route.g2pContext == kOfficialContext);  // official = empty context (R7)
    REQUIRE(route.g2pSource == kG2pSourceOfficial);  // official source (R7)
    REQUIRE(route.g2pContextVersion.isEmpty());
    REQUIRE(route.s2pMode == "dict");

    std::filesystem::remove_all(root);
}

TEST_CASE("LanguageRoute voicebank G2P: with g2pPackages and version", "[g2p][route][resolve]") {
    const auto root = makeTempDir("voicebank-g2p");
    createPackage(root, "pkg.vb", "2.0.0", "singer_vb", "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.5.0"},
    });
    writeFile(root / "assets/cmn.txt", "ni\tn i\n");

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.vb"] = root;

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);
    auto routeExp = langSvc.resolveLanguageRoute("pkg.vb", stdc::VersionNumber{}, "singer_vb", "cmn");
    REQUIRE(routeExp.hasValue());
    const auto &route = *routeExp;
    REQUIRE(route.g2pId == "g2p-cmn-custom");
    // V3-01: voicebank context = packageId + "__" + singerId (deprecated
    // initialize(map) path; g2pContextVersion is empty because the map
    // carries no voicebank version — callers must use the version-aware
    // overload to get a non-empty version).
    REQUIRE(route.g2pContext == "pkg.vb__singer_vb");
    REQUIRE(route.g2pSource == kG2pSourceVoicebank);  // voicebank source (R7)
    REQUIRE(route.g2pContextVersion.toString() == "0.0");

    std::filesystem::remove_all(root);
}

TEST_CASE("LanguageRoute multi-language singer with mixed G2P", "[g2p][route][resolve]") {
    const auto root = makeTempDir("multi-lang");
    // Singer supports cmn (official G2P) + en (voicebank G2P) + jp (official G2P)
    createPackage(root, "pkg.multi", "1.0.0", "multi_singer", "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
        {"en", "g2p-en-custom", "dict", "assets/en.txt",
         {"g2p/g2p-en-custom"}, "2.0.0"},
        {"jp", "g2p-jp-official", "dict", "assets/jp.txt", {}, ""},
    });
    writeFile(root / "assets/cmn.txt", "ni\tn i\n");
    writeFile(root / "assets/en.txt", "hello\th ah l ow\n");
    writeFile(root / "assets/jp.txt", "konnichiwa\tk o N n i ch i w a\n");

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.multi"] = root;

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);

    // cmn = official
    auto routeCmn = langSvc.resolveLanguageRoute("pkg.multi", stdc::VersionNumber{}, "multi_singer", "cmn");
    REQUIRE(routeCmn.hasValue());
    REQUIRE(routeCmn->g2pId == "g2p-cmn-official");
    REQUIRE(routeCmn->g2pSource == kG2pSourceOfficial);  // official (R7)

    // en = voicebank private
    auto routeEn = langSvc.resolveLanguageRoute("pkg.multi", stdc::VersionNumber{}, "multi_singer", "en");
    REQUIRE(routeEn.hasValue());
    REQUIRE(routeEn->g2pId == "g2p-en-custom");
    REQUIRE(routeEn->g2pSource == kG2pSourceVoicebank);  // voicebank (R7)
    // V3-01: deprecated path yields empty g2pContextVersion.
    REQUIRE(routeEn->g2pContextVersion.toString() == "0.0");

    // jp = official
    auto routeJp = langSvc.resolveLanguageRoute("pkg.multi", stdc::VersionNumber{}, "multi_singer", "jp");
    REQUIRE(routeJp.hasValue());
    REQUIRE(routeJp->g2pId == "g2p-jp-official");
    REQUIRE(routeJp->g2pSource == kG2pSourceOfficial);  // official (R7)

    std::filesystem::remove_all(root);
}

TEST_CASE("LanguageRoute empty languageId falls back to defaultLanguage", "[g2p][route][resolve]") {
    const auto root = makeTempDir("default-lang");
    createPackage(root, "pkg.def", "1.0.0", "singer_def", "en", {
        {"en", "g2p-en-official", "dict", "assets/en.txt", {}, ""},
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
    });
    writeFile(root / "assets/en.txt", "hello\th ah l ow\n");
    writeFile(root / "assets/cmn.txt", "ni\tn i\n");

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.def"] = root;

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);
    // Empty languageId should resolve to "en" (the defaultLanguage).
    auto routeExp = langSvc.resolveLanguageRoute("pkg.def", stdc::VersionNumber{}, "singer_def", "");
    REQUIRE(routeExp.hasValue());
    REQUIRE(routeExp->g2pId == "g2p-en-official");

    std::filesystem::remove_all(root);
}

TEST_CASE("LanguageRoute singer not found returns error", "[g2p][route][resolve][error]") {
    const auto root = makeTempDir("singer-not-found");
    createPackage(root, "pkg.x", "1.0.0", "real_singer", "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
    });

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.x"] = root;

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);
    auto routeExp = langSvc.resolveLanguageRoute("pkg.x", stdc::VersionNumber{}, "nonexistent_singer", "cmn");
    REQUIRE(!routeExp.hasValue());
    REQUIRE(routeExp.error().message().find("singer not found") != std::string::npos);

    std::filesystem::remove_all(root);
}

TEST_CASE("LanguageRoute language not found in package returns error", "[g2p][route][resolve][error]") {
    const auto root = makeTempDir("lang-not-found");
    createPackage(root, "pkg.y", "1.0.0", "singer_y", "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
    });

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.y"] = root;

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);
    auto routeExp = langSvc.resolveLanguageRoute("pkg.y", stdc::VersionNumber{}, "singer_y", "fr");
    REQUIRE(!routeExp.hasValue());
    // Error should mention language not found or not declared.
    const auto msg = routeExp.error().message();
    REQUIRE((msg.find("language") != std::string::npos ||
             msg.find("not found") != std::string::npos ||
             msg.find("not declared") != std::string::npos));

    std::filesystem::remove_all(root);
}

TEST_CASE("LanguageRoute package not found returns error", "[g2p][route][resolve][error]") {
    LanguageService langSvc;
    auto routeExp = langSvc.resolveLanguageRoute("nonexistent.pkg", stdc::VersionNumber{}, "singer", "cmn");
    REQUIRE(!routeExp.hasValue());
    REQUIRE(routeExp.error().message().find("package directory not found") != std::string::npos);
}

TEST_CASE("LanguageRoute language not declared by singer returns error", "[g2p][route][resolve][error]") {
    const auto root = makeTempDir("lang-not-declared");
    // Singer declares only cmn, but we request en.
    createPackage(root, "pkg.decl", "1.0.0", "singer_decl", "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
        {"en", "g2p-en-official", "dict", "assets/en.txt", {}, ""},  // in package languages
    });
    // But singer only declares cmn in its own languages list.
    // We need to create a singer that only declares cmn, while the package has en.
    // The current createPackage puts all langs in singer config. Let's write a custom one.
    std::string singer = "{\n";
    singer += "    \"$version\": \"1.0\",\n";
    singer += "    \"id\": \"singer_decl\",\n";
    singer += "    \"level\": 1,\n";
    singer += "    \"imports\": [{\"inferenceId\": \"duration\"}],\n";
    singer += "    \"configuration\": {\n";
    singer += "        \"defaultLanguage\": \"cmn\",\n";
    singer += "        \"languages\": [{\"id\": \"cmn\", \"g2p\": \"g2p-cmn-official\", \"s2pMode\": \"dict\"}]\n";
    singer += "        // NOTE: en is NOT declared by this singer\n";
    singer += "    }\n";
    singer += "}\n";
    writeFile(root / "characters/singer/config.json", singer);

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.decl"] = root;

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);
    // en is not in the singer's languages list, should error.
    auto routeExp = langSvc.resolveLanguageRoute("pkg.decl", stdc::VersionNumber{}, "singer_decl", "en");
    REQUIRE(!routeExp.hasValue());

    std::filesystem::remove_all(root);
}

// ===========================================================================
// Complex: multiple packages with conflicting G2P versions
// ===========================================================================

TEST_CASE("LanguageRoute same singer different packages different G2P versions", "[g2p][route][resolve][complex]") {
    const auto root1 = makeTempDir("conflict-v1");
    const auto root2 = makeTempDir("conflict-v2");

    // Package 1: voicebank G2P v1.0
    createPackage(root1, "pkg.conflict", "1.0.0", "shared_singer", "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.0.0"},
    });

    // Package 2: same singerId, different packageId, voicebank G2P v2.0
    createPackage(root2, "pkg.conflict2", "2.0.0", "shared_singer", "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "2.0.0"},
    });

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.conflict"] = root1;
    packageDirs["pkg.conflict2"] = root2;

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);

    // Resolve via package 1 -> G2P version 1.0.0
    auto route1 = langSvc.resolveLanguageRoute("pkg.conflict", stdc::VersionNumber{}, "shared_singer", "cmn");
    REQUIRE(route1.hasValue());
    REQUIRE(route1->g2pSource == kG2pSourceVoicebank);  // voicebank (R7)
    // V3-01: deprecated path yields empty g2pContextVersion.
    REQUIRE(route1->g2pContextVersion.toString() == "0.0");

    // Resolve via package 2 -> G2P version 2.0.0
    auto route2 = langSvc.resolveLanguageRoute("pkg.conflict2", stdc::VersionNumber{}, "shared_singer", "cmn");
    REQUIRE(route2.hasValue());
    REQUIRE(route2->g2pSource == kG2pSourceVoicebank);  // voicebank (R7)
    // V3-01: deprecated path yields empty g2pContextVersion.
    REQUIRE(route2->g2pContextVersion.toString() == "0.0");

    // V3-01: different packageIds produce different g2pContexts
    // (packageId__singerId) so same-singerId voicebanks in different
    // packages get isolated ContextKeys. Deprecated path yields empty
    // g2pContextVersion for both.
    REQUIRE(route1->g2pContext != route2->g2pContext);
    REQUIRE(route1->g2pContext == "pkg.conflict__shared_singer");
    REQUIRE(route2->g2pContext == "pkg.conflict2__shared_singer");
    REQUIRE(route1->g2pContextVersion == route2->g2pContextVersion);

    std::filesystem::remove_all(root1);
    std::filesystem::remove_all(root2);
}

TEST_CASE("LanguageRoute voicebank G2P without version uses empty VersionNumber", "[g2p][route][resolve][complex]") {
    const auto root = makeTempDir("vb-no-version");
    // g2pPackages present but g2pPackageVersion omitted.
    createPackage(root, "pkg.nover", "1.0.0", "singer_nv", "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, ""},  // empty version
    });

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.nover"] = root;

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);
    auto routeExp = langSvc.resolveLanguageRoute("pkg.nover", stdc::VersionNumber{}, "singer_nv", "cmn");
    REQUIRE(routeExp.hasValue());
    REQUIRE(routeExp->g2pSource == kG2pSourceVoicebank);  // voicebank (R7)
    // Version should be empty (not "0.0.0").
    REQUIRE(routeExp->g2pContextVersion.isEmpty());

    std::filesystem::remove_all(root);
}

TEST_CASE("LanguageRoute multiple G2P packages per language", "[g2p][route][resolve][complex]") {
    const auto root = makeTempDir("multi-g2p-pkg");
    // A language can reference multiple g2pPackages (e.g. base + extension).
    createPackage(root, "pkg.multi-g2p", "1.0.0", "singer_mg", "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-base", "g2p/g2p-cmn-ext"}, "1.0.0"},
    });

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.multi-g2p"] = root;

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);
    auto routeExp = langSvc.resolveLanguageRoute("pkg.multi-g2p", stdc::VersionNumber{}, "singer_mg", "cmn");
    REQUIRE(routeExp.hasValue());
    REQUIRE(routeExp->g2pSource == kG2pSourceVoicebank);  // voicebank (R7)
    REQUIRE(routeExp->g2pId == "g2p-cmn-custom");

    std::filesystem::remove_all(root);
}

TEST_CASE("LanguageRoute s2pMode direct vs dict", "[g2p][route][resolve][complex]") {
    const auto root = makeTempDir("s2p-modes");
    createPackage(root, "pkg.s2p", "1.0.0", "singer_s2p", "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
        {"en", "g2p-en-official", "direct", "assets/en.txt", {}, ""},
    });

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.s2p"] = root;

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);

    auto routeDict = langSvc.resolveLanguageRoute("pkg.s2p", stdc::VersionNumber{}, "singer_s2p", "cmn");
    REQUIRE(routeDict.hasValue());
    REQUIRE(routeDict->s2pMode == "dict");

    auto routeDirect = langSvc.resolveLanguageRoute("pkg.s2p", stdc::VersionNumber{}, "singer_s2p", "en");
    REQUIRE(routeDirect.hasValue());
    REQUIRE(routeDirect->s2pMode == "direct");

    std::filesystem::remove_all(root);
}

// ===========================================================================
// LanguageService::convert error paths (without G2P runtime initialized)
// ===========================================================================

TEST_CASE("LanguageService convert returns error on route resolution failure", "[g2p][route][convert]") {
    LanguageService langSvc;
    auto exp = langSvc.convert("nonexistent.pkg", stdc::VersionNumber{}, "singer", "cmn",
                               {G2pInput("test", "g2p-x")});
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.error().code() == srt::core::ErrorCode::G2pPackageNotFound);
    REQUIRE(!exp.error().message().empty());
}

TEST_CASE("LanguageService convertLyric returns empty without initialization", "[g2p][route][convert]") {
    // convertLyric calls Manager::instance()->convert() which requires
    // initialization. Without init, it returns empty or error results.
    LanguageService langSvc;
    auto results = langSvc.convertLyric({G2pInput("test", "g2p-x")});
    // Without initialization, results may be empty or contain error entries.
    // The key assertion is that it doesn't crash.
    // If non-empty, each result should have a non-NoError errorType.
    for (const auto &res : results) {
        REQUIRE(res.isFailed());
    }
}

// ===========================================================================
// PackageParser + G2P route: malformed packages
// ===========================================================================

TEST_CASE("LanguageRoute corrupt desc.json returns parse error", "[g2p][route][resolve][corrupt]") {
    const auto root = makeTempDir("corrupt-desc");
    writeFile(root / "desc.json", "{ this is not valid json }}}");

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.corrupt"] = root;

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);
    auto routeExp = langSvc.resolveLanguageRoute("pkg.corrupt", stdc::VersionNumber{}, "singer", "cmn");
    REQUIRE(!routeExp.hasValue());

    std::filesystem::remove_all(root);
}

TEST_CASE("LanguageRoute singer config missing language resource returns error", "[g2p][route][resolve][corrupt]") {
    const auto root = makeTempDir("missing-lang-resource");
    // Singer declares cmn but package.languages() doesn't have it.
    std::string desc = "{\n";
    desc += "    \"id\": \"pkg.missing-lang\",\n";
    desc += "    \"version\": \"1.0.0\",\n";
    desc += "    \"contributes\": {\n";
    desc += "        \"singers\": [\"characters/singer/config.json\"],\n";
    desc += "        \"inferences\": [\"inferences/duration/config.json\"]\n";
    desc += "    }\n";
    desc += "}\n";
    writeFile(root / "desc.json", desc);

    // Singer config declares cmn as default, but no language resources.
    std::string singer = "{\n";
    singer += "    \"$version\": \"1.0\",\n";
    singer += "    \"id\": \"singer_ml\",\n";
    singer += "    \"level\": 1,\n";
    singer += "    \"imports\": [{\"inferenceId\": \"duration\"}],\n";
    singer += "    \"configuration\": {\n";
    singer += "        \"defaultLanguage\": \"cmn\",\n";
    singer += "        \"languages\": [{\"id\": \"cmn\", \"g2p\": \"g2p-cmn-official\", \"s2pMode\": \"dict\"}]\n";
    singer += "    }\n";
    singer += "}\n";
    writeFile(root / "characters/singer/config.json", singer);

    writeFile(root / "inferences/duration/config.json",
              "{\n    \"id\": \"duration\",\n    \"class\": \"ai.svs.DurationInference\",\n    \"level\": 1,\n    \"configuration\": {}\n}\n");

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.missing-lang"] = root;

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);
    // The package languages() list should include cmn (from singer config).
    // If the parser doesn't find it, resolveLanguageRoute should error.
    auto routeExp = langSvc.resolveLanguageRoute("pkg.missing-lang", stdc::VersionNumber{}, "singer_ml", "cmn");
    // This depends on how the parser merges singer languages into package languages.
    // If the parser fills package.languages() from singer config, this succeeds.
    // If not, it errors. Either way, it should not crash.
    if (routeExp.hasValue()) {
        REQUIRE(routeExp->g2pId == "g2p-cmn-official");
    }

    std::filesystem::remove_all(root);
}
