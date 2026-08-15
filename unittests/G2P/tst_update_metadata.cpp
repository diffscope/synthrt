// LanguageService::updateMetadata hot-reload tests (V3-16).
//
// Verifies the incremental metadata update path described in
// docs/refactoring-v3/06-verification-cross-platform-hotreload.md:
//   - updateMetadata returns a correct PackageDirectoryDiff (added / removed /
//     unchanged) against the previously-registered packageDirs.
//   - Added voicebank G2P contexts are registered in the Manager.
//   - Removed voicebank G2P contexts are cleaned up via
//     PackageManager::removeContextsByPrefix.
//   - updateMetadata is rejected with G2pAlreadyInitialized after
//     initializeModels() and with G2pInitializationError before
//     initializeMetadata().
//
// Note: Manager is a process-wide singleton; contexts accumulate across tests.
// Each test uses a unique packageId to avoid context-name collisions and
// checks for presence of its own ContextKeys rather than asserting the total
// context count. Tests that require a real Manager::initialize() (ONNX models)
// are marked SKIP() so the metadata-only paths can still be verified in L1.

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Support/ContextKey.h>
#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/Expected.h>
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
                   ("srt-g2p-upd-" + name + "-" + std::to_string(stamp));
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

    // Create a minimal voicebank package on disk. Mirrors the helper in
    // test_language_service_isolation.cpp so each test file stays
    // independently compilable.
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
        // directory (pkgDir/characters/singer/).
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

    bool managerHasContext(const std::string &context,
                           const stdc::VersionNumber &version) {
        const auto keys = Manager::instance()->contextKeys();
        const srt::core::ContextKey target{context, version};
        return std::find(keys.begin(), keys.end(), target) != keys.end();
    }

    // Build a PackageDirectoryEntry vector from (packageId, version, path)
    // tuples. Keeps test bodies concise.
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
// 1. updateMetadata adds a new package: diff.added contains the new entry,
//    diff.unchanged contains the previously-registered entry.
// ===========================================================================

TEST_CASE("updateMetadata adds new package to diff.added",
          "[g2p][update-metadata]") {
    const auto root = makeTempDir("add-new");
    const std::string pkgA = "upd.add.a";
    const std::string pkgB = "upd.add.b";
    const std::string singerA = "add_singer_a";
    const std::string singerB = "add_singer_b";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();

    createPackage(root / "pkg_a", pkgA, "1.0.0", singerA, "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.5.0"},
    });
    createPackage(root / "pkg_b", pkgB, "1.0.0", singerB, "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.5.0"},
    });

    LanguageService langSvc;
    auto initExp = langSvc.initializeMetadata(
        {}, {}, makeEntries({{pkgA, v1, root / "pkg_a"}}));
    REQUIRE(initExp.hasValue());
    REQUIRE(langSvc.metadataReady());

    auto updExp = langSvc.updateMetadata(
        {}, {}, makeEntries({{pkgA, v1, root / "pkg_a"},
                             {pkgB, v1, root / "pkg_b"}}));
    REQUIRE(updExp.hasValue());
    const auto &diff = *updExp;

    REQUIRE(diff.added.size() == 1);
    REQUIRE(diff.added[0].packageId == pkgB);
    REQUIRE(diff.added[0].version == v1);

    REQUIRE(diff.unchanged.size() == 1);
    REQUIRE(diff.unchanged[0].packageId == pkgA);

    REQUIRE(diff.removed.empty());

    // The added package's voicebank G2P context is now registered in the
    // Manager (Stage 1.3 mirror for added entries).
    const auto ctxB = pkgB + "__" + singerB;
    REQUIRE(managerHasContext(ctxB, v1));

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 2. updateMetadata removes a package: diff.removed contains the retired
//    entry, and the Manager context for that package is cleaned up.
// ===========================================================================

TEST_CASE("updateMetadata removes package and cleans Manager context",
          "[g2p][update-metadata]") {
    const auto root = makeTempDir("remove");
    const std::string pkgA = "upd.rm.a";
    const std::string pkgB = "upd.rm.b";
    const std::string singerB = "rm_singer_b";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();

    createPackage(root / "pkg_a", pkgA, "1.0.0", "rm_singer_a", "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.5.0"},
    });
    createPackage(root / "pkg_b", pkgB, "1.0.0", singerB, "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.5.0"},
    });

    LanguageService langSvc;
    auto initExp = langSvc.initializeMetadata(
        {}, {}, makeEntries({{pkgA, v1, root / "pkg_a"},
                             {pkgB, v1, root / "pkg_b"}}));
    REQUIRE(initExp.hasValue());

    const auto ctxB = pkgB + "__" + singerB;
    REQUIRE(managerHasContext(ctxB, v1));

    auto updExp = langSvc.updateMetadata(
        {}, {}, makeEntries({{pkgA, v1, root / "pkg_a"}}));
    REQUIRE(updExp.hasValue());
    const auto &diff = *updExp;

    REQUIRE(diff.removed.size() == 1);
    REQUIRE(diff.removed[0].packageId == pkgB);
    REQUIRE(diff.removed[0].version == v1);

    REQUIRE(diff.unchanged.size() == 1);
    REQUIRE(diff.unchanged[0].packageId == pkgA);
    REQUIRE(diff.added.empty());

    // removeContextsByPrefix was invoked for pkgB; the Manager no longer
    // holds the retired context. But if a prior test (G2P-003/004) already
    // called Manager::initialize() on the process-wide singleton, the
    // context is immutable and cannot be removed — this is the singleton's
    // contract, not a bug in updateMetadata.
    if (!Manager::instance()->initialized()) {
        REQUIRE_FALSE(managerHasContext(ctxB, v1));
    } else {
        // Singleton already initialized: removeContextsByPrefix rejected,
        // context remains. Documented test-isolation limitation.
        REQUIRE(managerHasContext(ctxB, v1));
    }

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 3. updateMetadata with identical input: everything ends up in unchanged,
//    added/removed are empty.
// ===========================================================================

TEST_CASE("updateMetadata no-change diff is all unchanged",
          "[g2p][update-metadata]") {
    const auto root = makeTempDir("nochange");
    const std::string pkgA = "upd.nc.a";
    const std::string pkgB = "upd.nc.b";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();

    createPackage(root / "pkg_a", pkgA, "1.0.0", "nc_singer_a", "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.5.0"},
    });
    createPackage(root / "pkg_b", pkgB, "1.0.0", "nc_singer_b", "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.5.0"},
    });

    const auto entries = makeEntries({{pkgA, v1, root / "pkg_a"},
                                      {pkgB, v1, root / "pkg_b"}});

    LanguageService langSvc;
    REQUIRE(langSvc.initializeMetadata({}, {}, entries).hasValue());

    auto updExp = langSvc.updateMetadata({}, {}, entries);
    REQUIRE(updExp.hasValue());
    const auto &diff = *updExp;

    REQUIRE(diff.added.empty());
    REQUIRE(diff.removed.empty());
    REQUIRE(diff.unchanged.size() == 2);

    // unchanged is in deterministic std::map iteration order (sorted by
    // (packageId, version)). Verify both packages are present.
    std::set<std::string> unchangedIds;
    for (const auto &e : diff.unchanged) {
        unchangedIds.insert(e.packageId);
    }
    REQUIRE(unchangedIds.count(pkgA) == 1);
    REQUIRE(unchangedIds.count(pkgB) == 1);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 4. updateMetadata after initializeModels() returns G2pAlreadyInitialized.
//    This requires a real Manager::initialize() success, which needs official
//    G2P packages. In an L1 environment without ONNX models initializeModels()
//    fails and the test is SKIP'd; the guard is still exercised in L2.
// ===========================================================================

TEST_CASE("updateMetadata after modelsReady returns G2pAlreadyInitialized",
          "[g2p][update-metadata]") {
    const auto root = makeTempDir("after-models");
    const std::string pkgA = "upd.am.a";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();

    createPackage(root / "pkg_a", pkgA, "1.0.0", "am_singer", "cmn", {
        {"cmn", "g2p-cmn-official", "dict", "assets/cmn.txt", {}, ""},
    });

    LanguageService langSvc;
    REQUIRE(langSvc.initializeMetadata(
        {}, {}, makeEntries({{pkgA, v1, root / "pkg_a"}})).hasValue());

    auto modelsExp = langSvc.initializeModels();
    if (!modelsExp.hasValue()) {
        // Manager::initialize() failed: no official G2P packages are
        // available in the L1 fixture, so modelsReady stays false and the
        // G2pAlreadyInitialized guard cannot be triggered. Defer to L2.
        SKIP("L2: needs a real Manager::initialize() to set modelsReady=true");
    }

    REQUIRE(langSvc.modelsReady());

    auto updExp = langSvc.updateMetadata(
        {}, {}, makeEntries({{pkgA, v1, root / "pkg_a"}}));
    REQUIRE(!updExp.hasValue());
    REQUIRE(updExp.takeError().code() ==
            srt::core::ErrorCode::G2pAlreadyInitialized);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 5. updateMetadata before initializeMetadata returns G2pInitializationError.
// ===========================================================================

TEST_CASE("updateMetadata without initializeMetadata fails",
          "[g2p][update-metadata]") {
    LanguageService langSvc;
    REQUIRE_FALSE(langSvc.metadataReady());

    auto updExp = langSvc.updateMetadata({}, {}, {});
    REQUIRE(!updExp.hasValue());
    REQUIRE(updExp.takeError().code() ==
            srt::core::ErrorCode::G2pInitializationError);

    // metadataReady is still false (guard rejected the call before any work).
    REQUIRE_FALSE(langSvc.metadataReady());
}

// ===========================================================================
// 6. removeContextsByPrefix verification: registering a package registers
//    its voicebank G2P context in the Manager; removing the package via
//    updateMetadata clears that context from the Manager.
// ===========================================================================

TEST_CASE("updateMetadata clears Manager context via removeContextsByPrefix",
          "[g2p][update-metadata]") {
    const auto root = makeTempDir("ctx-cleanup");
    const std::string pkgA = "upd.ctx.a";
    const std::string singerA = "ctx_singer_a";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();

    createPackage(root / "pkg_a", pkgA, "1.0.0", singerA, "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.5.0"},
    });

    LanguageService langSvc;
    REQUIRE(langSvc.initializeMetadata(
        {}, {}, makeEntries({{pkgA, v1, root / "pkg_a"}})).hasValue());

    const auto ctxA = pkgA + "__" + singerA;
    REQUIRE(managerHasContext(ctxA, v1));

    // Removing the package via updateMetadata triggers
    // PackageManager::removeContextsByPrefix("upd.ctx.a__") internally.
    auto updExp = langSvc.updateMetadata({}, {}, {});
    REQUIRE(updExp.hasValue());
    const auto &diff = *updExp;
    REQUIRE(diff.removed.size() == 1);
    REQUIRE(diff.removed[0].packageId == pkgA);

    // Singleton-isolation-aware assertion (see test 2 for rationale).
    if (!Manager::instance()->initialized()) {
        REQUIRE_FALSE(managerHasContext(ctxA, v1));
    } else {
        REQUIRE(managerHasContext(ctxA, v1));
    }

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 7. D-43: multi-version same-packageId hot reload. When two versions of the
//    same packageId coexist, removing one version must NOT retire the other
//    version's voicebank G2P context. The single-arg removeContextsByPrefix
//    overload matched every version and corrupted coexisting versions
//    (D-24 violation); the version-aware overload (D-43) fixes this.
// ===========================================================================

TEST_CASE("updateMetadata multi-version hot reload preserves coexisting version context",
          "[g2p][update-metadata][d43]") {
    const auto root = makeTempDir("multi-ver");
    const std::string pkgA = "upd.mv.a";
    const std::string singerA = "mv_singer_a";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();
    const auto v2 = stdc::VersionNumber::fromString("2.0.0").value();

    // Two on-disk packages sharing packageId+singerId but different versions.
    createPackage(root / "pkg_a_v1", pkgA, "1.0.0", singerA, "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.5.0"},
    });
    createPackage(root / "pkg_a_v2", pkgA, "2.0.0", singerA, "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.5.0"},
    });

    LanguageService langSvc;
    REQUIRE(langSvc.initializeMetadata(
        {}, {}, makeEntries({{pkgA, v1, root / "pkg_a_v1"},
                             {pkgA, v2, root / "pkg_a_v2"}})).hasValue());

    const auto ctxA = pkgA + "__" + singerA;
    // Both versions' contexts must be registered.
    REQUIRE(managerHasContext(ctxA, v1));
    REQUIRE(managerHasContext(ctxA, v2));

    // Hot reload: retire v1 only. v2 must remain untouched.
    auto updExp = langSvc.updateMetadata(
        {}, {}, makeEntries({{pkgA, v2, root / "pkg_a_v2"}}));
    REQUIRE(updExp.hasValue());
    const auto &diff = *updExp;

    REQUIRE(diff.removed.size() == 1);
    REQUIRE(diff.removed[0].packageId == pkgA);
    REQUIRE(diff.removed[0].version == v1);

    REQUIRE(diff.unchanged.size() == 1);
    REQUIRE(diff.unchanged[0].packageId == pkgA);
    REQUIRE(diff.unchanged[0].version == v2);

    // D-43 core assertion: v1 context retired, v2 context preserved.
    // The pre-fix single-arg removeContextsByPrefix(prefix) would have
    // retired BOTH versions, leaving v2 unavailable for G2P routing.
    // Singleton-isolation-aware assertion (see test 2 for rationale).
    if (!Manager::instance()->initialized()) {
        REQUIRE_FALSE(managerHasContext(ctxA, v1));
    } else {
        REQUIRE(managerHasContext(ctxA, v1));
    }
    REQUIRE(managerHasContext(ctxA, v2));

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 8. D-43 inverse: hot reload that retires v2 (the higher version) must
//    equally preserve v1's context. Verifies the version filter is not
//    accidentally keyed on ordering.
// ===========================================================================

TEST_CASE("updateMetadata multi-version hot reload retires higher version preserves lower",
          "[g2p][update-metadata][d43]") {
    const auto root = makeTempDir("multi-ver-inv");
    const std::string pkgB = "upd.mv.b";
    const std::string singerB = "mv_singer_b";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();
    const auto v2 = stdc::VersionNumber::fromString("2.0.0").value();

    createPackage(root / "pkg_b_v1", pkgB, "1.0.0", singerB, "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.5.0"},
    });
    createPackage(root / "pkg_b_v2", pkgB, "2.0.0", singerB, "cmn", {
        {"cmn", "g2p-cmn-custom", "dict", "assets/cmn.txt",
         {"g2p/g2p-cmn-custom"}, "1.5.0"},
    });

    LanguageService langSvc;
    REQUIRE(langSvc.initializeMetadata(
        {}, {}, makeEntries({{pkgB, v1, root / "pkg_b_v1"},
                             {pkgB, v2, root / "pkg_b_v2"}})).hasValue());

    const auto ctxB = pkgB + "__" + singerB;
    REQUIRE(managerHasContext(ctxB, v1));
    REQUIRE(managerHasContext(ctxB, v2));

    // Retire v2; v1 must remain.
    auto updExp = langSvc.updateMetadata(
        {}, {}, makeEntries({{pkgB, v1, root / "pkg_b_v1"}}));
    REQUIRE(updExp.hasValue());
    const auto &diff = *updExp;

    REQUIRE(diff.removed.size() == 1);
    REQUIRE(diff.removed[0].packageId == pkgB);
    REQUIRE(diff.removed[0].version == v2);

    REQUIRE(managerHasContext(ctxB, v1));
    // Singleton-isolation-aware assertion (see test 2 for rationale).
    if (!Manager::instance()->initialized()) {
        REQUIRE_FALSE(managerHasContext(ctxB, v2));
    } else {
        REQUIRE(managerHasContext(ctxB, v2));
    }

    std::filesystem::remove_all(root);
}
