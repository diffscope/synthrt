// LanguageService 5-layer version isolation tests (V3-01).
//
// Verifies that multi-version same-packageId voicebanks coexist across all
// 5 layers described in docs/refactoring-v3/02-language-service-version-isolation.md:
//   1. Entry layer  — initializeMetadata(vector<PackageDirectoryEntry>) keeps
//      all (packageId, version) pairs (legacy unordered_map collapsed them).
//   2. Route layer   — resolveLanguageRoute(packageId, version, ...) routes to
//      the exact version; empty version + multiple matches → G2pVersionAmbiguous.
//   3. Manager ctx   — addPackagePath(context="packageId:singerId", version=
//      voicebankVersion, path) registers a distinct ContextKey per version.
//   4. S2P cache     — cache key includes version.toString() so multi-version
//      same-packageId entries get independent cache slots.
//   5. Convert layer — convert() fills inputs[i].g2pContext/g2pContextVersion
//      from the resolved route.
//
// Note: Manager is a process-wide singleton; contexts accumulate across tests.
// Each test uses a unique packageId to avoid context-name collisions and
// checks for presence of its own ContextKeys rather than asserting the total
// context count.

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Support/ContextKey.h>
#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/Core/Manager.h>
#include <synthrt/G2P/Core/PackageManager.h>
#include <synthrt/G2P/LanguageService.h>
#include <synthrt/G2P/LanguageRoute.h>

using namespace srt::g2p;

namespace {

    std::filesystem::path makeTempDir(const std::string &name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = std::filesystem::temp_directory_path() /
                   ("srt-lang-iso-" + name + "-" + std::to_string(stamp));
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

    // Create a minimal voicebank package on disk. The singer references a
    // voicebank private G2P subpackage (g2pPackages non-empty) so that
    // LanguageService registers a versioned voicebank context. The G2P
    // subpackage directory is created (empty) so addPackagePath succeeds
    // and a ContextKey is actually registered in the Manager.
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

        // Create G2P subpackage directories so addPackagePath succeeds and
        // ContextKeys are actually registered in the Manager. PackageParser
        // resolves g2pPackages paths relative to the singer config file's
        // directory (pkgDir/characters/singer/), so the directory must be
        // created at that resolved path, not at pkgDir/<g2pPkg>.
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

    // Check that a (context, version) ContextKey is present in the Manager.
    bool managerHasContext(const std::string &context,
                           const stdc::VersionNumber &version) {
        const auto keys = Manager::instance()->contextKeys();
        const srt::core::ContextKey target{context, version};
        return std::find(keys.begin(), keys.end(), target) != keys.end();
    }

} // namespace

// ===========================================================================
// Layer 1+2+3: multi-version same-packageId isolation
// Two entries with same packageId but different versions coexist; both
// resolveLanguageRoute succeed with different g2pContextVersion; the Manager
// has two distinct ContextKeys (context="pkg:singer", version=v1/v2).
// ===========================================================================

TEST_CASE("LanguageService multi-version isolation across 5 layers",
          "[g2p][isolation][v3-01]") {
    const auto root = makeTempDir("multi-version");
    const std::string packageId = "iso.multiver";
    const std::string singerId = "iso_singer";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0");
    const auto v2 = stdc::VersionNumber::fromString("2.0.0");

    // Both versions use the same G2P subpackage version (1.5.0) to verify
    // that isolation is driven by the voicebank version, NOT the G2P subpackage
    // version (V3-01 §2.4 / spec §七 last row).
    createPackage(root / "pkg_v1", packageId, "1.0.0", singerId, "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.5.0"},
    });
    createPackage(root / "pkg_v2", packageId, "2.0.0", singerId, "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.5.0"},
    });

    // Layer 1: entry — initializeMetadata with vector<PackageDirectoryEntry>
    // keeps both versions (legacy map would have collapsed to one).
    std::vector<PackageDirectoryEntry> entries = {
        {packageId, v1, root / "pkg_v1"},
        {packageId, v2, root / "pkg_v2"},
    };

    LanguageService langSvc;
    auto initExp = langSvc.initializeMetadata({}, {}, entries);
    REQUIRE(initExp.hasValue());
    REQUIRE(langSvc.metadataReady());

    // Layer 2: route — both versions resolve with the same g2pContext
    // ("packageId__singerId") but different g2pContextVersion.
    auto route1 = langSvc.resolveLanguageRoute(packageId, v1, singerId, "cmn");
    REQUIRE(route1.hasValue());
    REQUIRE(route1->g2pId == "g2p-cmn-custom");
    REQUIRE(route1->g2pContext == (packageId + "__" + singerId));
    REQUIRE(route1->g2pContextVersion == v1);
    REQUIRE(route1->g2pSource == kG2pSourceVoicebank);

    auto route2 = langSvc.resolveLanguageRoute(packageId, v2, singerId, "cmn");
    REQUIRE(route2.hasValue());
    REQUIRE(route2->g2pId == "g2p-cmn-custom");
    REQUIRE(route2->g2pContext == (packageId + "__" + singerId));
    REQUIRE(route2->g2pContextVersion == v2);
    REQUIRE(route2->g2pSource == kG2pSourceVoicebank);

    // The two versions must produce different g2pContextVersion values —
    // this is the actual isolation key at the Manager layer.
    REQUIRE(route1->g2pContextVersion != route2->g2pContextVersion);

    // Layer 3: Manager context table — two distinct ContextKeys, one per
    // voicebank version. Context name is shared ("pkg__singer"), version
    // differs (v1 / v2). Even though the G2P subpackage g2pPackageVersion
    // is identical (1.5.0), the ContextKey versions differ because they
    // come from the voicebank package version (V3-01 §2.4).
    const auto ctxName = packageId + "__" + singerId;
    REQUIRE(managerHasContext(ctxName, v1));
    REQUIRE(managerHasContext(ctxName, v2));

    std::filesystem::remove_all(root);
}

// ===========================================================================
// Layer 2: G2pVersionAmbiguous — empty version + multiple matches
// ===========================================================================

TEST_CASE("LanguageService ambiguous without version returns G2pVersionAmbiguous",
          "[g2p][isolation][v3-01][ambiguous]") {
    const auto root = makeTempDir("ambiguous");
    const std::string packageId = "iso.ambiguous";
    const std::string singerId = "amb_singer";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0");
    const auto v2 = stdc::VersionNumber::fromString("2.0.0");

    createPackage(root / "pkg_v1", packageId, "1.0.0", singerId, "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.5.0"},
    });
    createPackage(root / "pkg_v2", packageId, "2.0.0", singerId, "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.5.0"},
    });

    std::vector<PackageDirectoryEntry> entries = {
        {packageId, v1, root / "pkg_v1"},
        {packageId, v2, root / "pkg_v2"},
    };

    LanguageService langSvc;
    REQUIRE(langSvc.initializeMetadata({}, {}, entries).hasValue());

    // Empty version + multiple matches → G2pVersionAmbiguous.
    auto route = langSvc.resolveLanguageRoute(
        packageId, stdc::VersionNumber(), singerId, "cmn");
    REQUIRE(!route.hasValue());
    auto err = route.takeError();
    REQUIRE(err.code() == srt::core::ErrorCode::G2pVersionAmbiguous);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// Layer 2: empty version + single match — backward compat with single-version
// ===========================================================================

TEST_CASE("LanguageService empty version + single match succeeds",
          "[g2p][isolation][v3-01][single-version]") {
    const auto root = makeTempDir("single-version");
    const std::string packageId = "iso.single";
    const std::string singerId = "single_singer";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0");

    createPackage(root / "pkg_v1", packageId, "1.0.0", singerId, "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.5.0"},
    });

    std::vector<PackageDirectoryEntry> entries = {
        {packageId, v1, root / "pkg_v1"},
    };

    LanguageService langSvc;
    REQUIRE(langSvc.initializeMetadata({}, {}, entries).hasValue());

    // Empty version + single match → use it (backward compat).
    auto route = langSvc.resolveLanguageRoute(
        packageId, stdc::VersionNumber(), singerId, "cmn");
    REQUIRE(route.hasValue());
    REQUIRE(route->g2pContext == (packageId + "__" + singerId));
    // Effective version is the single registered version.
    REQUIRE(route->g2pContextVersion == v1);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// Layer 2: non-empty version not in packageDirs → G2pPackageNotFound
// ===========================================================================

TEST_CASE("LanguageService unknown version returns G2pPackageNotFound",
          "[g2p][isolation][v3-01][not-found]") {
    const auto root = makeTempDir("unknown-version");
    const std::string packageId = "iso.unknown";
    const std::string singerId = "unk_singer";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0");
    const auto v9 = stdc::VersionNumber::fromString("9.9.9");

    createPackage(root / "pkg_v1", packageId, "1.0.0", singerId, "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.5.0"},
    });

    std::vector<PackageDirectoryEntry> entries = {
        {packageId, v1, root / "pkg_v1"},
    };

    LanguageService langSvc;
    REQUIRE(langSvc.initializeMetadata({}, {}, entries).hasValue());

    auto route = langSvc.resolveLanguageRoute(packageId, v9, singerId, "cmn");
    REQUIRE(!route.hasValue());
    REQUIRE(route.takeError().code() ==
            srt::core::ErrorCode::G2pPackageNotFound);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// Layer 1: PackageDuplicate — same (packageId, version) twice
// ===========================================================================

TEST_CASE("LanguageService duplicate packageId+version returns PackageDuplicate",
          "[g2p][isolation][v3-01][duplicate]") {
    const auto root = makeTempDir("duplicate");
    const std::string packageId = "iso.duplicate";
    const std::string singerId = "dup_singer";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0");

    createPackage(root / "pkg_a", packageId, "1.0.0", singerId, "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.5.0"},
    });
    createPackage(root / "pkg_b", packageId, "1.0.0", singerId, "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.5.0"},
    });

    std::vector<PackageDirectoryEntry> entries = {
        {packageId, v1, root / "pkg_a"},
        {packageId, v1, root / "pkg_b"},  // same (packageId, version)
    };

    LanguageService langSvc;
    auto initExp = langSvc.initializeMetadata({}, {}, entries);
    REQUIRE(!initExp.hasValue());
    REQUIRE(initExp.takeError().code() ==
            srt::core::ErrorCode::PackageDuplicate);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// Layer 2: official G2P (no g2pPackages) — g2pContext = kOfficialContext,
// g2pContextVersion empty, g2pSource = "official"
// ===========================================================================

TEST_CASE("LanguageService official G2P route has empty context",
          "[g2p][isolation][v3-01][official]") {
    const auto root = makeTempDir("official");
    const std::string packageId = "iso.official";
    const std::string singerId = "off_singer";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0");

    // No g2pPackages → official G2P route.
    createPackage(root / "pkg_off", packageId, "1.0.0", singerId, "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
    });

    std::vector<PackageDirectoryEntry> entries = {
        {packageId, v1, root / "pkg_off"},
    };

    LanguageService langSvc;
    REQUIRE(langSvc.initializeMetadata({}, {}, entries).hasValue());

    auto route = langSvc.resolveLanguageRoute(packageId, v1, singerId, "cmn");
    REQUIRE(route.hasValue());
    REQUIRE(route->g2pId == "g2p-cmn-official");
    REQUIRE(route->g2pContext == kOfficialContext);
    REQUIRE(route->g2pContextVersion.isEmpty());
    REQUIRE(route->g2pSource == kG2pSourceOfficial);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// Legacy deprecated overloads — suppress warning, verify single-version
// backward compat. The deprecated initializeMetadata(map) delegates to the
// version-aware overload with empty version; the deprecated
// resolveLanguageRoute(3-arg) delegates with empty version.
// ===========================================================================

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

TEST_CASE("LanguageService legacy overloads single-version backward compat",
          "[g2p][isolation][v3-01][legacy]") {
    const auto root = makeTempDir("legacy");
    const std::string packageId = "iso.legacy";
    const std::string singerId = "leg_singer";

    createPackage(root / "pkg_leg", packageId, "1.0.0", singerId, "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
    });

    // Legacy map overload — single entry, empty version.
    std::unordered_map<std::string, std::filesystem::path> pkgDirs = {
        {packageId, root / "pkg_leg"},
    };

    LanguageService langSvc;
    REQUIRE(langSvc.initializeMetadata({}, {}, pkgDirs).hasValue());

    // Legacy 3-arg resolveLanguageRoute — empty version, single match → ok.
    auto route = langSvc.resolveLanguageRoute(packageId, singerId, "cmn");
    REQUIRE(route.hasValue());
    REQUIRE(route->g2pId == "g2p-cmn-official");
    REQUIRE(route->g2pSource == kG2pSourceOfficial);

    std::filesystem::remove_all(root);
}

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
