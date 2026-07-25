// VoicebankScanner.packageDirectories multi-version tests (V3-01 §1.6, 5th layer).
//
// Verifies that VoicebankScanner preserves all (packageId, version) pairs
// during refresh() so multi-version same-packageId voicebanks survive in the
// returned vector. The legacy packageDirectory(packageId) overload (deprecated)
// collapses to the first discovered entry — callers needing version isolation
// must migrate to packageDirectories(packageId).
//
// Related spec: docs/refactoring-v3/02-language-service-version-isolation.md §1.6

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <stdcorelib/path.h>
#include <stdcorelib/support/versionnumber.h>

#include <diffsinger/Bank/VoicebankScanner.h>

using namespace ds::bank;

namespace {

    std::filesystem::path makeTempDir(const std::string &name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = std::filesystem::temp_directory_path() /
                   ("srt-vb-pkgdirs-" + name + "-" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        return dir;
    }

    void writeFile(const std::filesystem::path &path, const std::string &text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << text;
    }

    // Create a minimal voicebank package. The singerId is fixed to
    // "test_singer" because the scanner dedups by (packageId, version), not
    // by singerId — what matters here is that two packages with the same
    // packageId but different versions both survive in packageDirs.
    void createPackage(const std::filesystem::path &pkgDir,
                       const std::string &packageId,
                       const std::string &version) {
        std::string desc = "{\n";
        desc += "    \"id\": \"" + packageId + "\",\n";
        desc += "    \"version\": \"" + version + "\",\n";
        desc += "    \"contributes\": {\n";
        desc += "        \"singers\": [\"characters/singer/config.json\"],\n";
        desc += "        \"inferences\": [\"inferences/duration/config.json\"]\n";
        desc += "    }\n";
        desc += "}\n";
        writeFile(pkgDir / "desc.json", desc);

        std::string singer = "{\n";
        singer += "    \"$version\": \"1.0\",\n";
        singer += "    \"id\": \"test_singer\",\n";
        singer += "    \"level\": 1,\n";
        singer += "    \"imports\": [{\"inferenceId\": \"duration\"}],\n";
        singer += "    \"configuration\": {\n";
        singer += "        \"defaultLanguage\": \"cmn\",\n";
        singer += "        \"languages\": [{\"id\": \"cmn\", \"g2p\": \"g2p-cmn-official\", \"s2pMode\": \"dict\"}]\n";
        singer += "    }\n";
        singer += "}\n";
        writeFile(pkgDir / "characters/singer/config.json", singer);

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
// packageDirectories(packageId): multi-version same-packageId coexist
// ===========================================================================

TEST_CASE("VoicebankScanner packageDirectories returns all versions",
          "[ds-bank][scanner][v3-01][package-dirs]") {
    const auto root = makeTempDir("multi-version");
    const std::string packageId = "pkgdirs.multiver";
    const auto pkgDir1 = root / "pkg_v1";
    const auto pkgDir2 = root / "pkg_v2";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0");
    const auto v2 = stdc::VersionNumber::fromString("2.0.0");

    createPackage(pkgDir1, packageId, "1.0.0");
    createPackage(pkgDir2, packageId, "2.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    auto refreshExp = scanner.refresh();
    REQUIRE(refreshExp.hasValue());
    // Both packages valid → two snapshots (singerId is shared, but
    // (packageId, version) differs).
    REQUIRE(scanner.singers().size() == 2);

    const auto dirs = scanner.packageDirectories(packageId);
    REQUIRE(dirs.size() == 2);

    // Both versions must be present (order = discovery order: v1 then v2).
    std::set<std::string> versionStrs;
    for (const auto &d : dirs) {
        versionStrs.insert(d.version.toString());
    }
    REQUIRE(versionStrs.count(v1.toString()) == 1);
    REQUIRE(versionStrs.count(v2.toString()) == 1);

    // Paths must match the created directories (lexically normal for
    // cross-platform comparison).
    std::set<std::string> pathStrs;
    for (const auto &d : dirs) {
        pathStrs.insert(stdc::path::to_utf8(d.path.lexically_normal()));
    }
    REQUIRE(pathStrs.count(stdc::path::to_utf8(pkgDir1.lexically_normal())) == 1);
    REQUIRE(pathStrs.count(stdc::path::to_utf8(pkgDir2.lexically_normal())) == 1);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// packageDirectories(packageId): unknown packageId → empty vector
// ===========================================================================

TEST_CASE("VoicebankScanner packageDirectories unknown packageId returns empty",
          "[ds-bank][scanner][v3-01][package-dirs]") {
    const auto root = makeTempDir("unknown");
    createPackage(root / "pkg_a", "pkgdirs.known", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();

    const auto dirs = scanner.packageDirectories("pkgdirs.nonexistent");
    REQUIRE(dirs.empty());

    std::filesystem::remove_all(root);
}

// ===========================================================================
// packageDirectories(packageId): same (packageId, version) seen twice across
// search paths → updates existing entry, no duplicate
// ===========================================================================

TEST_CASE("VoicebankScanner packageDirectories dedups same version",
          "[ds-bank][scanner][v3-01][package-dirs]") {
    const auto root1 = makeTempDir("dedup-1");
    const auto root2 = makeTempDir("dedup-2");
    const std::string packageId = "pkgdirs.dedup";
    const auto v1 = stdc::VersionNumber::fromString("1.0.0");

    // Same packageId+version in two search paths. The second refresh
    // overwrites the first's path (matches legacy overwrite-on-same-packageId
    // behavior for the single-version case).
    createPackage(root1 / "pkg", packageId, "1.0.0");
    createPackage(root2 / "pkg", packageId, "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root1, root2});
    scanner.refresh();

    const auto dirs = scanner.packageDirectories(packageId);
    REQUIRE(dirs.size() == 1);
    REQUIRE(dirs[0].version == v1);

    std::filesystem::remove_all(root1);
    std::filesystem::remove_all(root2);
}

// ===========================================================================
// Legacy deprecated packageDirectory(packageId): returns first discovered
// entry's path (single-version backward compat). Multi-version same-packageId
// collapses to one entry here.
// ===========================================================================

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

TEST_CASE("VoicebankScanner packageDirectory legacy returns first",
          "[ds-bank][scanner][v3-01][package-dirs][legacy]") {
    const auto root = makeTempDir("legacy");
    const std::string packageId = "pkgdirs.legacy";
    const auto pkgDir1 = root / "pkg_v1";

    createPackage(pkgDir1, packageId, "1.0.0");
    createPackage(root / "pkg_v2", packageId, "2.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();

    // Legacy overload returns a single path (first discovered).
    const auto dir = scanner.packageDirectory(packageId);
    REQUIRE(!dir.empty());
    REQUIRE(dir.lexically_normal() == pkgDir1.lexically_normal());

    std::filesystem::remove_all(root);
}

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
