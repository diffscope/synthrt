// LanguageService convert / convertLyric / resolveS2pResource basic tests.
//
// Covers readiness state transitions, convert error paths (route resolution
// failures), convertLyric behavior without ONNX initialization, S2P resource
// resolution, and the official G2P routing path. These are L1 tests: they do
// not load ONNX models, so initializeModels() may fail and tests that depend
// on a real Manager::initialize() guard their assertions accordingly.
//
// Note: Manager is a process-wide singleton; tests share it. Each test uses
// a unique packageId to avoid context-name collisions.

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
                   ("srt-g2p-conv-" + name + "-" + std::to_string(stamp));
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
// 1. Readiness state transitions:
//    - New LanguageService: metadataReady/modelsReady/ready all false.
//    - After initializeMetadata: metadataReady=true, others false.
//    - After initializeModels: modelsReady depends on whether the Manager
//      singleton can be initialized (needs real G2P packages). Both paths
//      are documented.
// ===========================================================================

TEST_CASE("LanguageService readiness state transitions",
          "[g2p][convert][readiness]") {
    const auto root = makeTempDir("readiness");
    const std::string pkgA = "conv.readiness";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0");

    createPackage(root / "pkg_a", pkgA, "1.0.0", "rd_singer", "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
    });

    LanguageService langSvc;

    // Fresh instance: all readiness flags false.
    REQUIRE_FALSE(langSvc.metadataReady());
    REQUIRE_FALSE(langSvc.modelsReady());
    REQUIRE_FALSE(langSvc.ready());

    // Stage 1: initializeMetadata sets metadataReady only.
    REQUIRE(langSvc.initializeMetadata(
        {}, {}, makeEntries({{pkgA, v1, root / "pkg_a"}})).hasValue());
    REQUIRE(langSvc.metadataReady());
    REQUIRE_FALSE(langSvc.modelsReady());
    REQUIRE_FALSE(langSvc.ready());

    // Stage 2: initializeModels may succeed (Manager already initialized by
    // another test) or fail (no official G2P packages in L1 fixture).
    auto modelsExp = langSvc.initializeModels();
    if (modelsExp.hasValue()) {
        REQUIRE(langSvc.modelsReady());
        REQUIRE(langSvc.ready());
    } else {
        // Manager::initialize() failed: modelsReady stays false, ready() is
        // still false. Route resolution remains available.
        REQUIRE_FALSE(langSvc.modelsReady());
        REQUIRE_FALSE(langSvc.ready());
    }

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 2. convertLyric when modelsReady=false: Manager::convert returns results
//    with errorType=NotInitialized (or UnknownError when initialized but the
//    g2pId has no registered task). Either way each result isFailed().
//    convertLyric does NOT guard on modelsReady — it delegates directly to
//    Manager::convert(), so the result shape is determined by the Manager
//    singleton state.
// ===========================================================================

TEST_CASE("convertLyric without modelsReady returns failed results",
          "[g2p][convert][convert-lyric]") {
    LanguageService langSvc;
    REQUIRE_FALSE(langSvc.modelsReady());

    // Use a g2pId that no registered task will match. Even if the Manager
    // singleton is initialized, the lookup misses and Manager::convert falls
    // back to copy mode with UnknownError (isFailed() == true). When the
    // Manager is not initialized, results carry NotInitialized.
    auto results = langSvc.convertLyric({G2pInput("hello", "g2p-missing-id")});
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].isFailed());
    REQUIRE(results[0].lyric == "hello");
}

// ===========================================================================
// 3. convert with non-existent packageId: route resolution fails with
//    G2pPackageNotFound. No package setup needed.
// ===========================================================================

TEST_CASE("convert with unknown packageId returns G2pPackageNotFound",
          "[g2p][convert][error]") {
    LanguageService langSvc;
    const auto v1 = stdc::VersionNumber::fromString("1.0.0");

    auto exp = langSvc.convert(
        "nonexistent.conv.pkg", v1, "singer_x", "cmn",
        {G2pInput("test", "g2p-x")});
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.takeError().code() ==
            srt::core::ErrorCode::G2pPackageNotFound);
}

// ===========================================================================
// 4. convert with language not declared by singer: resolveLanguageRoute
//    returns G2pValidationError, which convert propagates.
// ===========================================================================

TEST_CASE("convert with unsupported language returns G2pValidationError",
          "[g2p][convert][error]") {
    const auto root = makeTempDir("unsupported-lang");
    const std::string pkgA = "conv.unsupported";
    const std::string singerId = "unsup_singer";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0");

    // Singer declares only cmn; requesting en must fail validation.
    createPackage(root / "pkg_a", pkgA, "1.0.0", singerId, "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
    });

    LanguageService langSvc;
    REQUIRE(langSvc.initializeMetadata(
        {}, {}, makeEntries({{pkgA, v1, root / "pkg_a"}})).hasValue());

    auto exp = langSvc.convert(pkgA, v1, singerId, "en",
                               {G2pInput("hello", "g2p-en")});
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.takeError().code() ==
            srt::core::ErrorCode::G2pValidationError);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 5. resolveS2pResource basic tests:
//    - Valid package+singer+language with a dict file returns a non-null
//      shared_ptr (dict mode resource).
//    - Invalid packageId returns an error from route resolution.
// ===========================================================================

TEST_CASE("resolveS2pResource returns resource for valid input",
          "[g2p][convert][s2p]") {
    const auto root = makeTempDir("s2p-valid");
    const std::string pkgA = "conv.s2p.valid";
    const std::string singerId = "s2p_valid_singer";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0");

    createPackage(root / "pkg_a", pkgA, "1.0.0", singerId, "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
    });
    // PackageParser resolves the dict path relative to the singer config
    // file's parent directory (<pkgDir>/characters/singer/), so the dict
    // file must exist at <pkgDir>/characters/singer/assets/cmn.txt for
    // LanguageResource::dictionary() to open it.
    writeFile(root / "pkg_a" / "characters" / "singer" / "assets" / "cmn.txt",
              "ni\tn i\n");

    LanguageService langSvc;
    REQUIRE(langSvc.initializeMetadata(
        {}, {}, makeEntries({{pkgA, v1, root / "pkg_a"}})).hasValue());

    // First verify the route resolves (isolates route vs resource creation).
    auto routeExp = langSvc.resolveLanguageRoute(pkgA, v1, singerId, "cmn");
    REQUIRE(routeExp.hasValue());
    REQUIRE(routeExp->s2pMode == "dict");
    REQUIRE(!routeExp->s2pFile.empty());

    auto resExp = langSvc.resolveS2pResource(pkgA, stdc::VersionNumber{}, singerId, "cmn");
    REQUIRE(resExp.hasValue());
    REQUIRE(*resExp != nullptr);

    std::filesystem::remove_all(root);
}

TEST_CASE("resolveS2pResource with unknown packageId returns error",
          "[g2p][convert][s2p]") {
    LanguageService langSvc;

    auto resExp = langSvc.resolveS2pResource(
        "nonexistent.s2p.pkg", stdc::VersionNumber{}, "singer_x", "cmn");
    REQUIRE(!resExp.hasValue());
    REQUIRE(resExp.takeError().code() ==
            srt::core::ErrorCode::G2pPackageNotFound);
}

// ===========================================================================
// 6. resolveLanguageRoute official G2P path: a package without g2pPackages
//    routes to kOfficialContext with g2pSource=kG2pSourceOfficial and an
//    empty g2pContextVersion.
// ===========================================================================

TEST_CASE("resolveLanguageRoute official G2P uses kOfficialContext",
          "[g2p][convert][route]") {
    const auto root = makeTempDir("official-route");
    const std::string pkgA = "conv.official.route";
    const std::string singerId = "off_route_singer";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0");

    // No g2pPackages → official G2P route.
    createPackage(root / "pkg_a", pkgA, "1.0.0", singerId, "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
    });

    LanguageService langSvc;
    REQUIRE(langSvc.initializeMetadata(
        {}, {}, makeEntries({{pkgA, v1, root / "pkg_a"}})).hasValue());

    auto routeExp = langSvc.resolveLanguageRoute(pkgA, v1, singerId, "cmn");
    REQUIRE(routeExp.hasValue());
    REQUIRE(routeExp->g2pId == "g2p-cmn-official");
    REQUIRE(routeExp->g2pContext == kOfficialContext);
    REQUIRE(routeExp->g2pContextVersion.isEmpty());
    REQUIRE(routeExp->g2pSource == kG2pSourceOfficial);

    std::filesystem::remove_all(root);
}
