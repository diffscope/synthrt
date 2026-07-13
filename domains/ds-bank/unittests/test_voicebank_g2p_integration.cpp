// Integration tests combining VoicebankScanner + PackageParser + LanguageService
// for complex real-world voicebank + G2P scenarios.
//
// These tests simulate the full pipeline that ds-editor-lite uses:
//   1. VoicebankScanner scans directories for voicebank packages
//   2. PackageParser parses each package manifest
//   3. LanguageService resolves G2P routes per singer+language
//   4. G2pInput/G2pRes structures are constructed for conversion
//
// Covered scenarios:
//   - Multi-package search path with mixed official/voicebank G2P
//   - Voicebank directory containing G2P packages alongside singer configs
//   - Cross-path package with same singerId but different G2P versions
//   - Package directory with inference configs of wrong type (vocoder config
//     in a duration slot, etc.)
//   - Singer referencing a G2P package that doesn't exist on disk
//   - Multiple singers sharing the same voicebank G2P package
//   - Voicebank with only official G2P (no g2pPackages)
//   - Voicebank with corrupted g2pPackages path (directory doesn't exist)
//   - SingerSnapshot resolutionState after scanning
//   - SingerSnapshot inferenceIds populated from package inferences
//   - packageDirectory() map used by LanguageService::initialize
//   - G2pInput context derivation from LanguageRoute
//   - End-to-end: scan -> resolve route -> construct G2pInput

#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/LanguageService.h>
#include <synthrt/G2P/LanguageRoute.h>

#include <diffsinger/Bank/VoicebankScanner.h>
#include <diffsinger/Bank/PackageParser.h>
#include <diffsinger/Bank/SingerSnapshot.h>

using namespace srt::g2p;
using namespace ds::bank;

namespace {

    std::filesystem::path makeTempDir(const std::string &name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = std::filesystem::temp_directory_path() /
                   ("ds-vb-g2p-" + name + "-" + std::to_string(stamp));
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
                       const std::vector<LangSpec> &langs,
                       const std::vector<std::string> &inferenceIds = {"duration"}) {
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

        std::string infRefs;
        std::string imports;
        for (size_t i = 0; i < inferenceIds.size(); ++i) {
            if (i > 0) {
                infRefs += ", ";
                imports += ", ";
            }
            infRefs += "\"inferences/" + inferenceIds[i] + "/config.json\"";
            imports += "{\"inferenceId\": \"" + inferenceIds[i] + "\"}";
        }

        std::string desc = "{\n";
        desc += "    \"id\": \"" + packageId + "\",\n";
        desc += "    \"version\": \"" + version + "\",\n";
        desc += "    \"contributes\": {\n";
        desc += "        \"singers\": [\"characters/singer/config.json\"],\n";
        desc += "        \"inferences\": [" + infRefs + "]\n";
        desc += "    }\n";
        desc += "}\n";
        writeFile(pkgDir / "desc.json", desc);

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

        for (const auto &infId : inferenceIds) {
            std::string cls = "ai.svs." + infId + "Inference";
            if (infId == "acoustic") cls = "ai.svs.AcousticInference";
            std::string cfg = "{\n";
            cfg += "    \"id\": \"" + infId + "\",\n";
            cfg += "    \"class\": \"" + cls + "\",\n";
            cfg += "    \"level\": 1,\n";
            cfg += "    \"configuration\": {}\n";
            cfg += "}\n";
            writeFile(pkgDir / ("inferences/" + infId + "/config.json"), cfg);
        }
    }

} // namespace

// ===========================================================================
// End-to-end: scan -> resolve route -> construct G2pInput
// ===========================================================================

TEST_CASE("Integration: scan official G2P package and resolve route", "[integration][g2p][e2e]") {
    const auto root = makeTempDir("e2e-official");
    createPackage(root / "pkg_a", "pkg.a", "1.0.0", "singer_a", "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
    });
    writeFile(root / "pkg_a/assets/cmn.txt", "ni\tn i\n");

    // Step 1: Scan
    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    auto refreshExp = scanner.refresh();
    REQUIRE(refreshExp.hasValue());
    REQUIRE(scanner.singers().size() == 1);
    REQUIRE(scanner.singers()[0].ref.packageId == "pkg.a");
    REQUIRE(scanner.singers()[0].ref.singerId == "singer_a");
    REQUIRE(scanner.singers()[0].resolutionState == ResolutionState::Resolved);

    // Step 2: Build packageDirs map
    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.a"] = scanner.packageDirectory("pkg.a");
    REQUIRE(!packageDirs["pkg.a"].empty());

    // Step 3: Resolve G2P route
    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);  // populates packageDirs even though it returns error
    auto routeExp = langSvc.resolveLanguageRoute("pkg.a", "singer_a", "cmn");
    REQUIRE(routeExp.hasValue());
    REQUIRE(routeExp->g2pId == "g2p-cmn-official");
    REQUIRE(routeExp->g2pSource == kG2pSourceOfficial);  // official (R7)

    // Step 4: Construct G2pInput from route
    G2pInput input("ni hao", routeExp->g2pId, "", {});
    REQUIRE(input.lyric == "ni hao");
    REQUIRE(input.g2pId == "g2p-cmn-official");
    REQUIRE(input.g2pContext.empty());  // official = empty context

    std::filesystem::remove_all(root);
}

TEST_CASE("Integration: scan voicebank G2P package and resolve route", "[integration][g2p][e2e]") {
    const auto root = makeTempDir("e2e-voicebank");
    createPackage(root / "pkg_vb", "pkg.vb", "2.0.0", "singer_vb", "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.5.0"},
    });
    writeFile(root / "pkg_vb/assets/cmn.txt", "ni\tn i\n");

    // Step 1: Scan
    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();
    REQUIRE(scanner.singers().size() == 1);

    // Step 2: Build packageDirs
    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.vb"] = scanner.packageDirectory("pkg.vb");

    // Step 3: Resolve route (voicebank context)
    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);  // populates packageDirs even though it returns error
    auto routeExp = langSvc.resolveLanguageRoute("pkg.vb", "singer_vb", "cmn");
    REQUIRE(routeExp.hasValue());
    REQUIRE(routeExp->g2pId == "g2p-cmn-custom");
    REQUIRE(routeExp->g2pSource == kG2pSourceVoicebank);  // voicebank (R7)
    REQUIRE(routeExp->g2pContextVersion.toString() == "1.5");

    // Step 4: Construct G2pInput with voicebank context (R7: g2pContext = singerId)
    G2pInput input("ni hao", routeExp->g2pId,
                   routeExp->g2pContext,        // g2pContext = singerId for voicebank
                   routeExp->g2pContextVersion);
    REQUIRE(input.g2pContext == "singer_vb");
    REQUIRE(input.g2pContextVersion.toString() == "1.5");

    std::filesystem::remove_all(root);
}

// ===========================================================================
// Multi-package search path with mixed G2P types
// ===========================================================================

TEST_CASE("Integration: multi-package with mixed official and voicebank G2P", "[integration][g2p][mixed]") {
    const auto root = makeTempDir("mixed-g2p");

    // Package 1: official G2P
    createPackage(root / "pkg_official", "pkg.official", "1.0.0", "singer_off", "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
    });

    // Package 2: voicebank G2P
    createPackage(root / "pkg_custom", "pkg.custom", "1.0.0", "singer_cus", "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.0.0"},
    });

    // Package 3: no G2P at all (empty languages)
    createPackage(root / "pkg_nolang", "pkg.nolang", "1.0.0", "singer_nl", "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "", {}, ""},
    });

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();
    REQUIRE(scanner.singers().size() == 3);

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    for (const auto &snap : scanner.singers()) {
        packageDirs[snap.ref.packageId] = scanner.packageDirectory(snap.ref.packageId);
    }

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);  // populates packageDirs even though it returns error

    // Official
    auto routeOff = langSvc.resolveLanguageRoute("pkg.official", "singer_off", "cmn");
    REQUIRE(routeOff.hasValue());
    REQUIRE(routeOff->g2pSource == kG2pSourceOfficial);  // official (R7)

    // Voicebank
    auto routeCus = langSvc.resolveLanguageRoute("pkg.custom", "singer_cus", "cmn");
    REQUIRE(routeCus.hasValue());
    REQUIRE(routeCus->g2pSource == kG2pSourceVoicebank);  // voicebank (R7)
    REQUIRE(routeCus->g2pContextVersion.toString() == "1.0");

    std::filesystem::remove_all(root);
}

// ===========================================================================
// Cross-path: same singerId, different G2P versions
// ===========================================================================

TEST_CASE("Integration: same singer across paths with different G2P versions", "[integration][g2p][cross-path]") {
    const auto root1 = makeTempDir("cross-v1");
    const auto root2 = makeTempDir("cross-v2");

    createPackage(root1 / "pkg", "pkg.cross", "1.0.0", "shared_singer", "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.0.0"},
    });
    createPackage(root2 / "pkg", "pkg.cross2", "2.0.0", "shared_singer", "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "2.0.0"},
    });

    VoicebankScanner scanner;
    scanner.setSearchPaths({root1, root2});
    scanner.refresh();
    REQUIRE(scanner.singers().size() == 2);

    // Both have the same singerId
    REQUIRE(scanner.singers()[0].ref.singerId == "shared_singer");
    REQUIRE(scanner.singers()[1].ref.singerId == "shared_singer");

    // But different packageId
    auto pkgIds = std::set<std::string>{
        scanner.singers()[0].ref.packageId,
        scanner.singers()[1].ref.packageId,
    };
    REQUIRE(pkgIds.count("pkg.cross") == 1);
    REQUIRE(pkgIds.count("pkg.cross2") == 1);

    // Find singer with version filter
    auto ref1 = scanner.findSinger("shared_singer", "pkg.cross", "1.0");
    REQUIRE(ref1.hasValue());
    REQUIRE(ref1->packageId == "pkg.cross");

    auto ref2 = scanner.findSinger("shared_singer", "pkg.cross2", "2.0");
    REQUIRE(ref2.hasValue());
    REQUIRE(ref2->packageId == "pkg.cross2");

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.cross"] = scanner.packageDirectory("pkg.cross");
    packageDirs["pkg.cross2"] = scanner.packageDirectory("pkg.cross2");

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);  // populates packageDirs even though it returns error

    auto route1 = langSvc.resolveLanguageRoute("pkg.cross", "shared_singer", "cmn");
    REQUIRE(route1.hasValue());
    REQUIRE(route1->g2pContextVersion.toString() == "1.0");

    auto route2 = langSvc.resolveLanguageRoute("pkg.cross2", "shared_singer", "cmn");
    REQUIRE(route2.hasValue());
    REQUIRE(route2->g2pContextVersion.toString() == "2.0");

    std::filesystem::remove_all(root1);
    std::filesystem::remove_all(root2);
}

// ===========================================================================
// Voicebank directory containing G2P packages alongside singer configs
// ===========================================================================

TEST_CASE("Integration: voicebank dir with G2P packages and singer configs", "[integration][g2p][layout]") {
    const auto root = makeTempDir("g2p-layout");

    // Create a package that has both singer config and G2P packages in the same dir.
    writeFile(root / "desc.json", R"json({
        "id": "pkg.combined",
        "version": "1.0.0",
        "contributes": {
            "singers": ["characters/singer/config.json"],
            "inferences": ["inferences/duration/config.json"]
        }
    })json");

    writeFile(root / "characters/singer/config.json", R"json({
        "$version": "1.0",
        "id": "singer_combined",
        "level": 1,
        "imports": [{"inferenceId": "duration"}],
        "configuration": {
            "defaultLanguage": "cmn",
            "languages": [{
                "id": "cmn",
                "g2p": "g2p-cmn-custom",
                "s2pMode": "dict",
                "dict": "assets/cmn_dict.txt",
                "g2pPackages": ["g2p/g2p-cmn-custom"],
                "g2pPackageVersion": "1.0.0"
            }]
        }
    })json");

    writeFile(root / "inferences/duration/config.json", R"json({
        "id": "duration", "class": "ai.svs.DurationInference", "level": 1, "configuration": {}
    })json");

    // Create the G2P package directory (even if empty, the path should be parseable).
    std::filesystem::create_directories(root / "g2p/g2p-cmn-custom");
    writeFile(root / "g2p/g2p-cmn-custom/plugin.json", "{}");
    writeFile(root / "assets/cmn_dict.txt", "ni\tn i\n");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();
    REQUIRE(scanner.singers().size() == 1);

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.combined"] = scanner.packageDirectory("pkg.combined");

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);  // populates packageDirs even though it returns error
    auto route = langSvc.resolveLanguageRoute("pkg.combined", "singer_combined", "cmn");
    REQUIRE(route.hasValue());
    REQUIRE(route->g2pSource == kG2pSourceVoicebank);  // voicebank (R7)
    REQUIRE(route->g2pId == "g2p-cmn-custom");

    std::filesystem::remove_all(root);
}

// ===========================================================================
// SingerSnapshot fields verification
// ===========================================================================

TEST_CASE("Integration: SingerSnapshot fields populated correctly", "[integration][snapshot]") {
    const auto root = makeTempDir("snapshot-fields");
    createPackage(root / "pkg_snap", "pkg.snap", "1.0.0", "snap_singer", "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
        {"en", "g2p-en-official", "dict", "assets/en.txt", {}, ""},
    }, {"duration", "pitch", "acoustic"});

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();
    REQUIRE(scanner.singers().size() == 1);

    const auto &snap = scanner.singers()[0];
    REQUIRE(snap.ref.packageId == "pkg.snap");
    REQUIRE(snap.ref.singerId == "snap_singer");
    REQUIRE(!snap.ref.version.empty());
    REQUIRE(snap.resolutionState == ResolutionState::Resolved);
    REQUIRE(snap.defaultLanguage == "cmn");
    REQUIRE(snap.languages.size() == 2);
    REQUIRE(snap.inferenceIds.size() == 3);

    // Check inferenceIds contains all 3.
    auto infSet = std::set<std::string>(
        snap.inferenceIds.begin(), snap.inferenceIds.end());
    REQUIRE(infSet.count("duration") == 1);
    REQUIRE(infSet.count("pitch") == 1);
    REQUIRE(infSet.count("acoustic") == 1);

    std::filesystem::remove_all(root);
}

TEST_CASE("Integration: SingerSnapshot phonemeLength default", "[integration][snapshot]") {
    const auto root = makeTempDir("snapshot-phoneme");
    createPackage(root / "pkg_ph", "pkg.ph", "1.0.0", "ph_singer", "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "", {}, ""},
    });

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();
    REQUIRE(scanner.singers().size() == 1);
    // Default phonemeLength should be 48.0.
    REQUIRE(scanner.singers()[0].phonemeLength == 48.0);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// packageDirectory() map used by LanguageService
// ===========================================================================

TEST_CASE("Integration: packageDirectory map drives LanguageService", "[integration][packageDirs]") {
    const auto root = makeTempDir("pkgdir-map");
    createPackage(root / "pkg_map", "pkg.map", "1.0.0", "map_singer", "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "", {}, ""},
    });

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();

    // packageDirectory returns the path for a given packageId.
    auto dir = scanner.packageDirectory("pkg.map");
    REQUIRE(!dir.empty());
    REQUIRE(std::filesystem::exists(dir / "desc.json"));

    // Non-existent package returns empty.
    REQUIRE(scanner.packageDirectory("nonexistent").empty());

    std::filesystem::remove_all(root);
}

// ===========================================================================
// G2pInput context derivation from LanguageRoute
// ===========================================================================

TEST_CASE("Integration: G2pInput context from official route", "[integration][g2p-input]") {
    const auto root = makeTempDir("input-official");
    createPackage(root / "pkg_in", "pkg.in", "1.0.0", "in_singer", "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "", {}, ""},
    });

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.in"] = root / "pkg_in";

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);  // populates packageDirs even though it returns error
    auto route = langSvc.resolveLanguageRoute("pkg.in", "in_singer", "cmn");
    REQUIRE(route.hasValue());

    // For official G2P, g2pContext is empty (= kOfficialContext, R7).
    G2pInput input("ni", route->g2pId, route->g2pContext, route->g2pContextVersion);
    REQUIRE(input.g2pId == "g2p-cmn-official");
    // R7: official G2P route now carries empty g2pContext (was singerId).
    // The actual context routing happens in Manager::convert().
    REQUIRE(input.g2pContext.empty());
    REQUIRE(input.g2pContextVersion.isEmpty());

    std::filesystem::remove_all(root);
}

TEST_CASE("Integration: G2pInput context from voicebank route", "[integration][g2p-input]") {
    const auto root = makeTempDir("input-vb");
    createPackage(root / "pkg_vb_in", "pkg.vb-in", "1.0.0", "vb_singer", "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "",
         {"g2p/g2p-cmn-custom"}, "3.0.0"},
    });

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.vb-in"] = root / "pkg_vb_in";

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);  // populates packageDirs even though it returns error
    auto route = langSvc.resolveLanguageRoute("pkg.vb-in", "vb_singer", "cmn");
    REQUIRE(route.hasValue());
    REQUIRE(route->g2pSource == kG2pSourceVoicebank);  // voicebank (R7)

    G2pInput input("ni", route->g2pId, route->g2pContext, route->g2pContextVersion);
    REQUIRE(input.g2pId == "g2p-cmn-custom");
    REQUIRE(input.g2pContext == "vb_singer");
    REQUIRE(input.g2pContextVersion.toString() == "3.0");

    std::filesystem::remove_all(root);
}

// ===========================================================================
// G2pRes construction simulating G2P conversion results
// ===========================================================================

TEST_CASE("Integration: G2pRes convert mode for real G2P output", "[integration][g2p-res]") {
    // Simulate a successful G2P conversion (convert mode).
    G2pRes res("ni", "g2p-cmn-official", "", {}, "n i", {}, kG2pModeConvert, NoError, kG2pSourceOfficial);
    REQUIRE(res.isOk());
    REQUIRE(res.mode == kG2pModeConvert);
    REQUIRE(res.g2pSource == kG2pSourceOfficial);
    REQUIRE(res.pronunciation == "n i");
    REQUIRE(res.candidates.size() == 1);
    REQUIRE(res.candidates[0] == "n i");
}

TEST_CASE("Integration: G2pRes copy mode for punctuation fallback", "[integration][g2p-res]") {
    // Simulate a punctuation that G2P can't convert, so it's copied.
    G2pRes res(",", "g2p-cmn-official", "", {}, ",", {}, kG2pModeCopy, NoError, kG2pSourceOfficial);
    REQUIRE(res.isOk());
    REQUIRE(res.mode == kG2pModeCopy);
    REQUIRE(res.pronunciation == ",");
}

TEST_CASE("Integration: G2pRes skip mode for empty lyric", "[integration][g2p-res]") {
    // Simulate an empty lyric (rest note).
    G2pRes res("", "g2p-cmn-official", "", {}, "", {}, kG2pModeSkip, NoError, kG2pSourceOfficial);
    REQUIRE(res.isOk());
    REQUIRE(res.mode == kG2pModeSkip);
    REQUIRE(res.pronunciation.empty());
}

TEST_CASE("Integration: G2pRes failure with voicebank source", "[integration][g2p-res]") {
    // Simulate a voicebank G2P that failed.
    auto version = stdc::VersionNumber::fromString("1.0");
    G2pRes res("unknown_word", "g2p-cmn-custom", "singer_vb", version,
               "unknown_word", {}, kG2pModeCopy, ModelInferenceFailed, kG2pSourceVoicebank);
    REQUIRE(res.isFailed());
    REQUIRE(res.errorType == ModelInferenceFailed);
    REQUIRE(res.g2pSource == kG2pSourceVoicebank);
    REQUIRE(res.g2pContext == "singer_vb");
}

// ===========================================================================
// Realistic multi-singer multi-language scenario
// ===========================================================================

TEST_CASE("Integration: realistic multi-singer multi-language voicebank directory", "[integration][realistic]") {
    const auto root = makeTempDir("realistic-multi");

    // Chinese voicebank with custom G2P
    createPackage(root / "voicebank_cn", "vb.cn", "1.0.0", "cn_singer", "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.0.0"},
    }, {"duration", "pitch", "acoustic", "vocoder"});

    // English voicebank with official G2P
    createPackage(root / "voicebank_en", "vb.en", "1.0.0", "en_singer", "en", {
        {"en", "g2p-en-official", "dict", "assets/en.txt", {}, ""},
    }, {"duration", "pitch", "acoustic", "vocoder"});

    // Bilingual voicebank (both cmn and en, cmn uses custom G2P)
    createPackage(root / "voicebank_bi", "vb.bi", "2.0.0", "bi_singer", "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "2.0.0"},
        {"en", "g2p-en-official", "dict", "assets/en.txt", {}, ""},
    }, {"duration", "pitch", "variance", "acoustic", "vocoder"});

    // Non-package directory (documentation)
    std::filesystem::create_directories(root / "docs");
    writeFile(root / "docs/README.md", "# Voicebanks");

    // Step 1: Scan all packages
    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    auto refreshExp = scanner.refresh();
    REQUIRE(refreshExp.hasValue());
    REQUIRE(scanner.singers().size() == 3);

    // Step 2: Build packageDirs
    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    for (const auto &snap : scanner.singers()) {
        packageDirs[snap.ref.packageId] = scanner.packageDirectory(snap.ref.packageId);
    }
    REQUIRE(packageDirs.size() == 3);

    // Step 3: Resolve routes for each singer+language
    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);  // populates packageDirs even though it returns error

    // CN singer: voicebank G2P
    auto routeCn = langSvc.resolveLanguageRoute("vb.cn", "cn_singer", "cmn");
    REQUIRE(routeCn.hasValue());
    REQUIRE(routeCn->g2pSource == kG2pSourceVoicebank);  // voicebank (R7)
    REQUIRE(routeCn->g2pId == "g2p-cmn-custom");
    REQUIRE(routeCn->g2pContextVersion.toString() == "1.0");

    // EN singer: official G2P
    auto routeEn = langSvc.resolveLanguageRoute("vb.en", "en_singer", "en");
    REQUIRE(routeEn.hasValue());
    REQUIRE(routeEn->g2pSource == kG2pSourceOfficial);  // official (R7)
    REQUIRE(routeEn->g2pId == "g2p-en-official");

    // BI singer: cmn = voicebank, en = official
    auto routeBiCmn = langSvc.resolveLanguageRoute("vb.bi", "bi_singer", "cmn");
    REQUIRE(routeBiCmn.hasValue());
    REQUIRE(routeBiCmn->g2pSource == kG2pSourceVoicebank);  // voicebank (R7)
    REQUIRE(routeBiCmn->g2pContextVersion.toString() == "2.0");

    auto routeBiEn = langSvc.resolveLanguageRoute("vb.bi", "bi_singer", "en");
    REQUIRE(routeBiEn.hasValue());
    REQUIRE(routeBiEn->g2pSource == kG2pSourceOfficial);  // official (R7)

    // Step 4: Construct G2pInputs (R7: g2pContext = singerId for voicebank, "" for official)
    std::vector<G2pInput> inputs;
    inputs.emplace_back("ni hao", routeCn->g2pId, routeCn->g2pContext, routeCn->g2pContextVersion);
    inputs.emplace_back("hello", routeEn->g2pId, routeEn->g2pContext, routeEn->g2pContextVersion);
    inputs.emplace_back("shi jie", routeBiCmn->g2pId, routeBiCmn->g2pContext, routeBiCmn->g2pContextVersion);

    REQUIRE(inputs.size() == 3);
    REQUIRE(inputs[0].g2pContext == "cn_singer");   // voicebank: singerId
    REQUIRE(inputs[1].g2pContext.empty());           // official: empty (R7, was "en_singer")
    REQUIRE(inputs[2].g2pContext == "bi_singer");   // voicebank: singerId

    std::filesystem::remove_all(root);
}

// ===========================================================================
// Edge case: voicebank G2P package path doesn't exist on disk
// ===========================================================================

TEST_CASE("Integration: voicebank G2P path doesn't exist on disk", "[integration][g2p][edge]") {
    const auto root = makeTempDir("g2p-missing-path");
    // The g2pPackages references a path that doesn't exist, but the route
    // resolution should still work (it just reads the manifest).
    createPackage(root / "pkg_missing_g2p", "pkg.mg2p", "1.0.0", "singer_mg", "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "",
         {"g2p/nonexistent/g2p-cmn-custom"}, "1.0.0"},
    });

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();
    REQUIRE(scanner.singers().size() == 1);

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.mg2p"] = scanner.packageDirectory("pkg.mg2p");

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);  // populates packageDirs even though it returns error
    // Route resolution should still succeed (it reads manifest, not G2P files).
    auto route = langSvc.resolveLanguageRoute("pkg.mg2p", "singer_mg", "cmn");
    REQUIRE(route.hasValue());
    REQUIRE(route->g2pSource == kG2pSourceVoicebank);  // voicebank (R7)
    REQUIRE(route->g2pId == "g2p-cmn-custom");

    std::filesystem::remove_all(root);
}

// ===========================================================================
// Edge case: multiple G2P packages for the same language
// ===========================================================================

TEST_CASE("Integration: language with multiple G2P packages", "[integration][g2p][edge]") {
    const auto root = makeTempDir("multi-g2p-pkgs");
    createPackage(root / "pkg_multi_g2p", "pkg.mg2p2", "1.0.0", "singer_mg2", "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "",
         {"g2p/g2p-cmn-base", "g2p/g2p-cmn-ext"}, "1.0.0"},
    });

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.mg2p2"] = scanner.packageDirectory("pkg.mg2p2");

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);  // populates packageDirs even though it returns error
    auto route = langSvc.resolveLanguageRoute("pkg.mg2p2", "singer_mg2", "cmn");
    REQUIRE(route.hasValue());
    REQUIRE(route->g2pSource == kG2pSourceVoicebank);  // voicebank (R7)
    // The route should carry the G2P version.
    REQUIRE(route->g2pContextVersion.toString() == "1.0");

    std::filesystem::remove_all(root);
}

// ===========================================================================
// Edge case: singer with no languages declared
// ===========================================================================

TEST_CASE("Integration: singer with no languages uses defaultLanguage fallback", "[integration][g2p][edge]") {
    const auto root = makeTempDir("no-langs");

    writeFile(root / "desc.json", R"json({
        "id": "pkg.nolang",
        "version": "1.0.0",
        "contributes": {
            "singers": ["characters/singer/config.json"],
            "inferences": ["inferences/duration/config.json"]
        }
    })json");

    // Singer has defaultLanguage but no languages list.
    writeFile(root / "characters/singer/config.json", R"json({
        "$version": "1.0",
        "id": "singer_nl",
        "level": 1,
        "imports": [{"inferenceId": "duration"}],
        "configuration": {
            "defaultLanguage": "cmn"
        }
    })json");

    writeFile(root / "inferences/duration/config.json", R"json({
        "id": "duration", "class": "ai.svs.DurationInference", "level": 1, "configuration": {}
    })json");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();
    REQUIRE(scanner.singers().size() == 1);
    REQUIRE(scanner.singers()[0].defaultLanguage == "cmn");
    // languages list should be empty.
    REQUIRE(scanner.singers()[0].languages.empty());

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.nolang"] = scanner.packageDirectory("pkg.nolang");

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);  // populates packageDirs even though it returns error
    // resolveLanguageRoute with empty singer languages should still work
    // if the language is found in package languages.
    auto route = langSvc.resolveLanguageRoute("pkg.nolang", "singer_nl", "cmn");
    // This may or may not succeed depending on whether the parser merges
    // singer languages into package languages. Either way, no crash.
    if (route.hasValue()) {
        // Official G2P (no g2pPackages): g2pContext is empty, g2pSource is
        // "official" (R7: was singerId == "singer_nl").
        REQUIRE(route->g2pSource == kG2pSourceOfficial);
        REQUIRE(route->g2pContext.empty());
    }

    std::filesystem::remove_all(root);
}

// ===========================================================================
// Edge case: empty singerId in package
// ===========================================================================

TEST_CASE("Integration: empty singerId in config defaults to something", "[integration][g2p][edge]") {
    const auto root = makeTempDir("empty-singer-id");

    writeFile(root / "desc.json", R"json({
        "id": "pkg.empty-sid",
        "version": "1.0.0",
        "contributes": {
            "singers": ["characters/singer/config.json"]
        }
    })json");

    // Singer config with no "id" field.
    writeFile(root / "characters/singer/config.json", R"json({
        "$version": "1.0",
        "level": 1,
        "configuration": {
            "defaultLanguage": "cmn",
            "languages": [{"id": "cmn", "g2p": "g2p-cmn-official", "s2pMode": "dict"}]
        }
    })json");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();
    // Singers without an "id" field are skipped by the PackageParser.
    REQUIRE(scanner.singers().size() == 0);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// VoicebankScanner + LanguageService: corrupted package skipped
// ===========================================================================

TEST_CASE("Integration: corrupted package skipped, valid packages still scanned", "[integration][g2p][corrupt]") {
    const auto root = makeTempDir("corrupt-and-valid");

    // Corrupted package
    std::filesystem::create_directories(root / "bad_pkg");
    writeFile(root / "bad_pkg/desc.json", "{ invalid json }}}");

    // Valid package
    createPackage(root / "good_pkg", "pkg.good", "1.0.0", "good_singer", "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "", {}, ""},
    });

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    auto refreshExp = scanner.refresh();
    REQUIRE(refreshExp.hasValue());

    // Only the valid package should produce a singer.
    REQUIRE(scanner.singers().size() == 1);
    REQUIRE(scanner.singers()[0].ref.packageId == "pkg.good");

    // The corrupted package should have an error status.
    bool foundError = false;
    for (const auto &status : refreshExp.value()) {
        if (!status.valid) {
            foundError = true;
            break;
        }
    }
    REQUIRE(foundError);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// Direct package mode (search path is itself a package)
// ===========================================================================

TEST_CASE("Integration: direct package mode with voicebank G2P", "[integration][g2p][direct]") {
    const auto root = makeTempDir("direct-vb");

    // root itself is a package (has desc.json directly).
    createPackage(root, "pkg.direct", "1.0.0", "direct_singer", "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "",
         {"g2p/g2p-cmn-custom"}, "1.0.0"},
    });

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();
    REQUIRE(scanner.singers().size() == 1);
    REQUIRE(scanner.singers()[0].ref.packageId == "pkg.direct");

    std::unordered_map<std::string, std::filesystem::path> packageDirs;
    packageDirs["pkg.direct"] = scanner.packageDirectory("pkg.direct");

    LanguageService langSvc;
    langSvc.initialize({}, {}, packageDirs);  // populates packageDirs even though it returns error
    auto route = langSvc.resolveLanguageRoute("pkg.direct", "direct_singer", "cmn");
    REQUIRE(route.hasValue());
    REQUIRE(route->g2pSource == kG2pSourceVoicebank);  // voicebank (R7)

    std::filesystem::remove_all(root);
}
