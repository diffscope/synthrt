// Complex scenario tests for VoicebankScanner.
//
// Covers real-world package directory complexity:
//   - Mixed directories: voicebank dirs containing non-voicebank subdirs
//   - Duplicate packages across multiple search paths (first wins for deprecated packageDirectory)
//   - Multi-version voicebanks with same packageId (packageDirectories preserves all versions)
//   - Invalid/corrupted desc.json
//   - Empty directories and non-directory files
//   - Search path that is itself a package (direct desc.json)
//   - Deeply nested non-package directories
//   - Package with multiple singers
//   - Non-package files in search root (README, LICENSE, etc.)

#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <diffsinger/Bank/VoicebankScanner.h>

using namespace ds::bank;

namespace {

    std::filesystem::path makeTempDir(const std::string &name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = std::filesystem::temp_directory_path() /
                   ("ds-bank-complex-" + name + "-" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        return dir;
    }

    void writeFile(const std::filesystem::path &path, const std::string &text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << text;
    }

    void createPackage(const std::filesystem::path &pkgDir,
                       const std::string &packageId,
                       const std::string &version,
                       const std::string &singerId = "test_singer") {
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
        singer += "    \"id\": \"" + singerId + "\",\n";
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

    void createMultiSingerPackage(const std::filesystem::path &pkgDir,
                                   const std::string &packageId,
                                   const std::string &version,
                                   const std::vector<std::string> &singerIds) {
        std::string singerList;
        for (size_t i = 0; i < singerIds.size(); ++i) {
            if (i > 0) singerList += ", ";
            singerList += "\"characters/" + singerIds[i] + "/config.json\"";
        }

        std::string desc = "{\n";
        desc += "    \"id\": \"" + packageId + "\",\n";
        desc += "    \"version\": \"" + version + "\",\n";
        desc += "    \"contributes\": {\n";
        desc += "        \"singers\": [" + singerList + "],\n";
        desc += "        \"inferences\": [\"inferences/duration/config.json\"]\n";
        desc += "    }\n";
        desc += "}\n";
        writeFile(pkgDir / "desc.json", desc);

        for (const auto &singerId : singerIds) {
            std::string singer = "{\n";
            singer += "    \"$version\": \"1.0\",\n";
            singer += "    \"id\": \"" + singerId + "\",\n";
            singer += "    \"level\": 1,\n";
            singer += "    \"imports\": [{\"inferenceId\": \"duration\"}],\n";
            singer += "    \"configuration\": {\n";
            singer += "        \"defaultLanguage\": \"cmn\",\n";
            singer += "        \"languages\": [{\"id\": \"cmn\", \"g2p\": \"g2p-cmn-official\", \"s2pMode\": \"dict\"}]\n";
            singer += "    }\n";
            singer += "}\n";
            writeFile(pkgDir / ("characters/" + singerId + "/config.json"), singer);
        }

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
// Mixed directory types: non-package files/dirs alongside packages
// ===========================================================================

TEST_CASE("VoicebankScanner mixed content: README and LICENSE files ignored", "[ds-bank][complex][mixed]") {
    const auto root = makeTempDir("mixed-files");

    // Create a valid package
    createPackage(root / "pkg_a", "pkg.a", "1.0.0");

    // Create non-package files in search root
    writeFile(root / "README.md", "# Voicebanks");
    writeFile(root / "LICENSE.txt", "MIT License");
    writeFile(root / ".gitignore", "*.tmp");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 1);
    REQUIRE(scanner.singers()[0].ref.packageId == "pkg.a");

    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankScanner mixed content: non-package subdirs ignored", "[ds-bank][complex][mixed]") {
    const auto root = makeTempDir("mixed-dirs");

    // Valid package
    createPackage(root / "pkg_a", "pkg.a", "1.0.0");

    // Non-package subdirectory (no desc.json)
    std::filesystem::create_directories(root / "documentation");
    writeFile(root / "documentation/guide.txt", "user guide");

    // Another non-package subdir with random content
    std::filesystem::create_directories(root / "temp_cache");
    writeFile(root / "temp_cache/cache.bin", "binary data");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 1);

    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankScanner nested non-package directories", "[ds-bank][complex][mixed]") {
    const auto root = makeTempDir("nested-nopkg");

    // Non-package subdir with nested content (no desc.json at any level scanner checks)
    std::filesystem::create_directories(root / "a/b/c/d");
    writeFile(root / "a/b/c/d/data.txt", "nested data");
    // Valid package in a direct subdirectory (scanner only checks direct children)
    createPackage(root / "pkg_direct", "pkg.deep", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 1);
    REQUIRE(scanner.singers()[0].ref.packageId == "pkg.deep");

    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankScanner empty directory as search path", "[ds-bank][complex][mixed]") {
    const auto root = makeTempDir("empty-root");
    // root is completely empty

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().empty());

    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankScanner empty string as search path", "[ds-bank][complex][mixed]") {
    VoicebankScanner scanner;
    scanner.setSearchPaths({""}); // empty path
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().empty());
}

TEST_CASE("VoicebankScanner non-existent path ignored", "[ds-bank][complex][mixed]") {
    VoicebankScanner scanner;
    scanner.setSearchPaths({"/nonexistent/path/that/does/not/exist"});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().empty());
}

// ===========================================================================
// Duplicate packages across search paths
// ===========================================================================

TEST_CASE("VoicebankScanner duplicate package across paths: first wins packageDirectory (deprecated)", "[ds-bank][complex][duplicate]") {
    const auto root1 = makeTempDir("dup-path1");
    const auto root2 = makeTempDir("dup-path2");

    // Same packageId in both paths
    createPackage(root1 / "pkg", "pkg.dup", "1.0.0");
    createPackage(root2 / "pkg", "pkg.dup", "2.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root1, root2});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());

    // Both singers are in snapshots
    REQUIRE(scanner.singers().size() == 2);

    // V3-01: deprecated packageDirectory(packageId) returns the first
    // discovered entry (root1/pkg). Multi-version same-packageId callers
    // should use packageDirectories(packageId) instead.
    auto dir = scanner.packageDirectory("pkg.dup");
    REQUIRE(!dir.empty());
    // The first path wins (root1/pkg)
    REQUIRE(dir.lexically_normal() == (root1 / "pkg").lexically_normal());

    std::filesystem::remove_all(root1);
    std::filesystem::remove_all(root2);
}

TEST_CASE("VoicebankScanner same singerId different packageId both found", "[ds-bank][complex][duplicate]") {
    const auto root = makeTempDir("same-singer-diff-pkg");

    // Two packages with same singerId but different packageId
    createPackage(root / "pkg_a", "pkg.a", "1.0.0", "shared_singer");
    createPackage(root / "pkg_b", "pkg.b", "1.0.0", "shared_singer");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 2);

    // Both snapshots should have the same singerId
    REQUIRE(scanner.singers()[0].ref.singerId == "shared_singer");
    REQUIRE(scanner.singers()[1].ref.singerId == "shared_singer");

    // But different packageId
    auto pkgIds = std::set<std::string>{
        scanner.singers()[0].ref.packageId,
        scanner.singers()[1].ref.packageId
    };
    REQUIRE(pkgIds.count("pkg.a") == 1);
    REQUIRE(pkgIds.count("pkg.b") == 1);

    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankScanner same packageId same version duplicate", "[ds-bank][complex][duplicate]") {
    const auto root1 = makeTempDir("same-pkg-path1");
    const auto root2 = makeTempDir("same-pkg-path2");

    // Exact same package in both paths
    createPackage(root1 / "pkg", "pkg.same", "1.0.0");
    createPackage(root2 / "pkg", "pkg.same", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root1, root2});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());

    // Both snapshots are present (no dedup)
    REQUIRE(scanner.singers().size() == 2);
    REQUIRE(scanner.singers()[0].ref.packageId == "pkg.same");
    REQUIRE(scanner.singers()[1].ref.packageId == "pkg.same");

    std::filesystem::remove_all(root1);
    std::filesystem::remove_all(root2);
}

// ===========================================================================
// Invalid/corrupted packages
// ===========================================================================

TEST_CASE("VoicebankScanner corrupted desc.json reports error status", "[ds-bank][complex][corrupt]") {
    const auto root = makeTempDir("corrupt-desc");

    // Create a directory with invalid JSON desc.json
    std::filesystem::create_directories(root / "bad_pkg");
    writeFile(root / "bad_pkg/desc.json", "{ this is not valid json }}}");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());

    // The bad package should produce an error status
    bool foundError = false;
    for (const auto &status : exp.value()) {
        if (!status.valid) {
            foundError = true;
            break;
        }
    }
    REQUIRE(foundError);
    REQUIRE(scanner.singers().empty());

    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankScanner empty desc.json reports error", "[ds-bank][complex][corrupt]") {
    const auto root = makeTempDir("empty-desc");

    std::filesystem::create_directories(root / "empty_pkg");
    writeFile(root / "empty_pkg/desc.json", "");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());

    bool foundError = false;
    for (const auto &status : exp.value()) {
        if (!status.valid) {
            foundError = true;
            break;
        }
    }
    REQUIRE(foundError);

    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankScanner missing singer config in package", "[ds-bank][complex][corrupt]") {
    const auto root = makeTempDir("missing-singer");

    // desc.json references a singer config that doesn't exist
    std::string desc = "{\n";
    desc += "    \"id\": \"pkg.missing\",\n";
    desc += "    \"version\": \"1.0.0\",\n";
    desc += "    \"contributes\": {\n";
    desc += "        \"singers\": [\"characters/nonexistent/config.json\"]\n";
    desc += "    }\n";
    desc += "}\n";
    std::filesystem::create_directories(root / "pkg_missing");
    writeFile(root / "pkg_missing/desc.json", desc);

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());

    // Package parse may fail or succeed with 0 singers
    // Just verify no crash
    bool foundError = false;
    for (const auto &status : exp.value()) {
        if (!status.valid) {
            foundError = true;
            break;
        }
    }
    // Either the package is invalid or has 0 singers
    if (!foundError) {
        REQUIRE(scanner.singers().empty());
    }

    std::filesystem::remove_all(root);
}

// ===========================================================================
// Search path that is itself a package (direct desc.json)
// ===========================================================================

TEST_CASE("VoicebankScanner search path is itself a package", "[ds-bank][complex][direct]") {
    const auto root = makeTempDir("direct-pkg");

    // The search path itself has desc.json (CLI pattern)
    createPackage(root, "pkg.direct", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 1);
    REQUIRE(scanner.singers()[0].ref.packageId == "pkg.direct");

    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankScanner direct package plus subdirectory packages", "[ds-bank][complex][direct]") {
    const auto root = makeTempDir("direct-plus-sub");

    // Root has its own desc.json
    createPackage(root, "pkg.root", "1.0.0");

    // Root also has subdirectory packages
    createPackage(root / "sub_pkg", "pkg.sub", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 2);

    auto pkgIds = std::set<std::string>{
        scanner.singers()[0].ref.packageId,
        scanner.singers()[1].ref.packageId
    };
    REQUIRE(pkgIds.count("pkg.root") == 1);
    REQUIRE(pkgIds.count("pkg.sub") == 1);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// Multi-singer packages
// ===========================================================================

TEST_CASE("VoicebankScanner multi-singer package", "[ds-bank][complex][multisinger]") {
    const auto root = makeTempDir("multi-singer");

    createMultiSingerPackage(root / "pkg_multi", "pkg.multi", "1.0.0",
                              {"singer_a", "singer_b", "singer_c"});

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 3);

    // All singers should belong to the same package
    for (const auto &s : scanner.singers()) {
        REQUIRE(s.ref.packageId == "pkg.multi");
    }

    // Each singer should be findable
    for (const auto &singerId : {"singer_a", "singer_b", "singer_c"}) {
        auto ref = scanner.findSinger(singerId, "pkg.multi", {});
        REQUIRE(ref.hasValue());
        REQUIRE(ref->singerId == singerId);
    }

    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankScanner multi-singer with shared singerId across packages", "[ds-bank][complex][multisinger]") {
    const auto root = makeTempDir("multi-singer-shared");

    // Package A has singer1 and singer2
    createMultiSingerPackage(root / "pkg_a", "pkg.a", "1.0.0", {"singer1", "singer2"});
    // Package B also has singer1 (different version)
    createPackage(root / "pkg_b", "pkg.b", "2.0.0", "singer1");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 3); // 2 + 1

    // findSinger("singer1") without packageId returns first match
    auto ref = scanner.findSinger("singer1");
    REQUIRE(ref.hasValue());
    REQUIRE(ref->singerId == "singer1");

    // findSinger with packageId filters correctly
    auto refA = scanner.findSinger("singer1", "pkg.a", {});
    REQUIRE(refA.hasValue());
    REQUIRE(refA->packageId == "pkg.a");

    auto refB = scanner.findSinger("singer1", "pkg.b", {});
    REQUIRE(refB.hasValue());
    REQUIRE(refB->packageId == "pkg.b");

    std::filesystem::remove_all(root);
}

// ===========================================================================
// Multiple search paths
// ===========================================================================

TEST_CASE("VoicebankScanner multiple search paths", "[ds-bank][complex][multipath]") {
    const auto root1 = makeTempDir("multipath-1");
    const auto root2 = makeTempDir("multipath-2");
    const auto root3 = makeTempDir("multipath-3");

    createPackage(root1 / "pkg1", "pkg.one", "1.0.0");
    createPackage(root2 / "pkg2", "pkg.two", "1.0.0");
    createPackage(root3 / "pkg3", "pkg.three", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root1, root2, root3});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 3);

    auto pkgIds = std::set<std::string>{
        scanner.singers()[0].ref.packageId,
        scanner.singers()[1].ref.packageId,
        scanner.singers()[2].ref.packageId
    };
    REQUIRE(pkgIds.count("pkg.one") == 1);
    REQUIRE(pkgIds.count("pkg.two") == 1);
    REQUIRE(pkgIds.count("pkg.three") == 1);

    std::filesystem::remove_all(root1);
    std::filesystem::remove_all(root2);
    std::filesystem::remove_all(root3);
}

TEST_CASE("VoicebankScanner search path with empty path in list", "[ds-bank][complex][multipath]") {
    const auto root1 = makeTempDir("with-empty-1");
    const auto root2 = makeTempDir("with-empty-2");

    createPackage(root1 / "pkg1", "pkg.one", "1.0.0");
    createPackage(root2 / "pkg2", "pkg.two", "1.0.0");

    VoicebankScanner scanner;
    // Empty path in the middle should be skipped
    scanner.setSearchPaths({root1, "", root2});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 2);

    std::filesystem::remove_all(root1);
    std::filesystem::remove_all(root2);
}

// ===========================================================================
// Clear and re-scan
// ===========================================================================

TEST_CASE("VoicebankScanner clear resets state", "[ds-bank][complex][clear]") {
    const auto root1 = makeTempDir("clear-1");
    const auto root2 = makeTempDir("clear-2");

    createPackage(root1 / "pkg1", "pkg.one", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root1});
    scanner.refresh();
    REQUIRE(scanner.singers().size() == 1);
    REQUIRE(!scanner.packageDirectory("pkg.one").empty());

    scanner.clear();
    REQUIRE(scanner.singers().empty());
    REQUIRE(scanner.packageDirectory("pkg.one").empty());

    // Re-scan with different path
    createPackage(root2 / "pkg2", "pkg.two", "1.0.0");
    scanner.setSearchPaths({root2});
    scanner.refresh();
    REQUIRE(scanner.singers().size() == 1);
    REQUIRE(scanner.singers()[0].ref.packageId == "pkg.two");

    std::filesystem::remove_all(root1);
    std::filesystem::remove_all(root2);
}

TEST_CASE("VoicebankScanner re-scan after adding packages", "[ds-bank][complex][clear]") {
    const auto root = makeTempDir("rescan");

    createPackage(root / "pkg1", "pkg.one", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();
    REQUIRE(scanner.singers().size() == 1);

    // Add a second package
    createPackage(root / "pkg2", "pkg.two", "1.0.0");

    // Re-scan should find both
    scanner.refresh();
    REQUIRE(scanner.singers().size() == 2);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// Package directory mapping
// ===========================================================================

TEST_CASE("VoicebankScanner packageDirectory for nonexistent package", "[ds-bank][complex][pkgdir]") {
    const auto root = makeTempDir("pkgdir-missing");
    createPackage(root / "pkg", "pkg.exists", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();

    auto dir = scanner.packageDirectory("nonexistent");
    REQUIRE(dir.empty());

    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankScanner packageDirectory after clear", "[ds-bank][complex][pkgdir]") {
    const auto root = makeTempDir("pkgdir-clear");
    createPackage(root / "pkg", "pkg.test", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();
    REQUIRE(!scanner.packageDirectory("pkg.test").empty());

    scanner.clear();
    REQUIRE(scanner.packageDirectory("pkg.test").empty());

    std::filesystem::remove_all(root);
}

// ===========================================================================
// Complex realistic scenario
// ===========================================================================

TEST_CASE("VoicebankScanner realistic multi-package setup", "[ds-bank][complex][scenario]") {
    const auto root = makeTempDir("realistic");

    // Package 1: Mandarin singer v1
    createPackage(root / "singer_zh_v1", "singer.zh", "1.0.0", "zh_singer");

    // Package 2: Mandarin singer v2 (same singerId, different version)
    createPackage(root / "singer_zh_v2", "singer.zh", "2.0.0", "zh_singer");

    // Package 3: English singer
    createPackage(root / "singer_en", "singer.en", "1.0.0", "en_singer");

    // Non-package directories
    std::filesystem::create_directories(root / "documentation");
    writeFile(root / "documentation/manual.txt", "manual");
    writeFile(root / "README.md", "# Voicebanks");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());

    // 3 singers: zh_singer (v1), zh_singer (v2), en_singer
    REQUIRE(scanner.singers().size() == 3);

    // Find zh_singer v1 (VersionNumber normalizes "1.0.0" to "1.0")
    auto refV1 = scanner.findSinger("zh_singer", "singer.zh", "1.0.0");
    REQUIRE(refV1.hasValue());
    REQUIRE(refV1->version == "1.0");

    // Find zh_singer v2
    auto refV2 = scanner.findSinger("zh_singer", "singer.zh", "2.0.0");
    REQUIRE(refV2.hasValue());
    REQUIRE(refV2->version == "2.0");

    // Find en_singer
    auto refEn = scanner.findSinger("en_singer", "singer.en", {});
    REQUIRE(refEn.hasValue());
    REQUIRE(refEn->packageId == "singer.en");

    // packageDirectory: singer.zh maps to last seen (v2 directory)
    auto dir = scanner.packageDirectory("singer.zh");
    REQUIRE(!dir.empty());

    // singer.en maps to its directory
    auto dirEn = scanner.packageDirectory("singer.en");
    REQUIRE(!dirEn.empty());
    REQUIRE(dirEn.lexically_normal() == (root / "singer_en").lexically_normal());

    std::filesystem::remove_all(root);
}
