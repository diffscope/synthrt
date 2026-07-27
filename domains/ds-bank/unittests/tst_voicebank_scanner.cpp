// Unit tests for ds::bank::VoicebankScanner version matching.
//
// Regression tests for BF-02 (findSnapshot ignores version) and BF-07
// (findSinger ignores version). Creates temp packages with the same singerId
// but different versions, then verifies that version-filtered lookups return
// the correct snapshot.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <diffsinger/Bank/VoicebankScanner.h>

using namespace ds::bank;

namespace {

    std::filesystem::path makeTempDir(const std::string &name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = std::filesystem::temp_directory_path() /
                   ("ds-bank-scanner-" + name + "-" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        return dir;
    }

    void writeFile(const std::filesystem::path &path, const std::string &text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << text;
    }

    // Create a minimal voicebank package directory with a singer.
    // All singers share the same singerId "test_singer" but different versions.
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

// ---------------------------------------------------------------------------
// BF-02: singerSnapshot matches version field
// ---------------------------------------------------------------------------

TEST_CASE("VoicebankScanner singerSnapshot matches version", "[ds-bank][scanner][bf-02]") {
    const auto root = makeTempDir("bf02-version-match");
    createPackage(root / "pkg_a", "pkg.a", "1.0.0");
    createPackage(root / "pkg_b", "pkg.b", "2.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    auto refreshExp = scanner.refresh();
    REQUIRE(refreshExp.hasValue());
    // Debug: check PackageStatus for errors
    for (const auto &status : refreshExp.value()) {
        if (!status.valid) {
            INFO("Package error: " << status.error.message);
        }
    }
    REQUIRE(scanner.singers().size() == 2);

    // BF-02 regression: singerSnapshot with version must return the correct snapshot.
    // Note: VersionNumber::toString() normalizes trailing zeros ("1.0.0" -> "1.0"),
    // but versionsMatch() uses semantic comparison so "1.0.0" still matches "1.0".
    SingerRef refA{"pkg.a", "test_singer", "1.0.0"};
    auto snapA = scanner.singerSnapshot(refA);
    REQUIRE(snapA.hasValue());
    REQUIRE(snapA->ref.packageId == "pkg.a");

    SingerRef refB{"pkg.b", "test_singer", "2.0.0"};
    auto snapB = scanner.singerSnapshot(refB);
    REQUIRE(snapB.hasValue());
    REQUIRE(snapB->ref.packageId == "pkg.b");

    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankScanner singerSnapshot empty version matches first", "[ds-bank][scanner][bf-02]") {
    const auto root = makeTempDir("bf02-empty-version");
    createPackage(root / "pkg_a", "pkg.a", "1.0.0");
    createPackage(root / "pkg_b", "pkg.b", "2.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();

    // Empty version should match any (backward compat).
    SingerRef ref{"pkg.a", "test_singer", ""};
    auto snap = scanner.singerSnapshot(ref);
    REQUIRE(snap.hasValue());
    REQUIRE(snap->ref.packageId == "pkg.a");

    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankScanner singerSnapshot wrong version returns error", "[ds-bank][scanner][bf-02]") {
    const auto root = makeTempDir("bf02-wrong-version");
    createPackage(root / "pkg_a", "pkg.a", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();

    // Non-empty version that doesn't match should return error.
    SingerRef ref{"pkg.a", "test_singer", "9.9.9"};
    auto snap = scanner.singerSnapshot(ref);
    REQUIRE(!snap.hasValue());

    std::filesystem::remove_all(root);
}

// ---------------------------------------------------------------------------
// BF-07: findSinger with packageId/version filtering
// ---------------------------------------------------------------------------

TEST_CASE("VoicebankScanner findSinger three-arg filters by packageId", "[ds-bank][scanner][bf-07]") {
    const auto root = makeTempDir("bf07-pkg-filter");
    createPackage(root / "pkg_a", "pkg.a", "1.0.0");
    createPackage(root / "pkg_b", "pkg.b", "2.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();
    REQUIRE(scanner.singers().size() == 2);

    // Both packages have singerId "test_singer"; filter by packageId.
    auto refA = scanner.findSinger("test_singer", "pkg.a", {});
    REQUIRE(refA.hasValue());
    REQUIRE(refA->packageId == "pkg.a");

    auto refB = scanner.findSinger("test_singer", "pkg.b", {});
    REQUIRE(refB.hasValue());
    REQUIRE(refB->packageId == "pkg.b");

    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankScanner findSinger three-arg filters by version", "[ds-bank][scanner][bf-07]") {
    const auto root = makeTempDir("bf07-version-filter");
    createPackage(root / "pkg_a", "pkg.a", "1.0.0");
    createPackage(root / "pkg_b", "pkg.b", "2.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();

    // Filter by version (packageId empty = no packageId filter).
    // VersionNumber normalizes "1.0.0" to "1.0" but versionsMatch() handles
    // the semantic equality, so querying "1.0.0" matches stored "1.0".
    auto ref1 = scanner.findSinger("test_singer", {}, "1.0.0");
    REQUIRE(ref1.hasValue());
    REQUIRE(ref1->packageId == "pkg.a");

    auto ref2 = scanner.findSinger("test_singer", {}, "2.0.0");
    REQUIRE(ref2.hasValue());
    REQUIRE(ref2->packageId == "pkg.b");

    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankScanner findSinger three-arg no match returns error", "[ds-bank][scanner][bf-07]") {
    const auto root = makeTempDir("bf07-no-match");
    createPackage(root / "pkg_a", "pkg.a", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();

    // Non-matching version should return error.
    auto ref = scanner.findSinger("test_singer", {}, "9.9.9");
    REQUIRE(!ref.hasValue());

    // Non-matching packageId should return error.
    auto ref2 = scanner.findSinger("test_singer", "nonexistent", {});
    REQUIRE(!ref2.hasValue());

    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankScanner findSinger single-arg backward compat", "[ds-bank][scanner][bf-07]") {
    const auto root = makeTempDir("bf07-single-arg");
    createPackage(root / "pkg_a", "pkg.a", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();

    // Single-arg overload should still work (returns first match).
    auto ref = scanner.findSinger("test_singer");
    REQUIRE(ref.hasValue());
    REQUIRE(ref->singerId == "test_singer");

    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankScanner findSinger not found returns error", "[ds-bank][scanner]") {
    const auto root = makeTempDir("find-not-found");
    createPackage(root / "pkg_a", "pkg.a", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();

    auto ref = scanner.findSinger("nonexistent_singer");
    REQUIRE(!ref.hasValue());

    auto ref2 = scanner.findSinger("nonexistent_singer", "pkg.a", "1.0.0");
    REQUIRE(!ref2.hasValue());

    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankScanner packageDirectory returns correct path", "[ds-bank][scanner]") {
    const auto root = makeTempDir("pkg-dir");
    const auto pkgDir = root / "pkg_a";
    createPackage(pkgDir, "pkg.a", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();

    auto dirs = scanner.packageDirectories("pkg.a");
    auto dir = dirs.empty() ? std::filesystem::path{} : dirs.front().path;
    REQUIRE(!dir.empty());
    REQUIRE(std::filesystem::path(dir).lexically_normal() ==
            pkgDir.lexically_normal());

    std::filesystem::remove_all(root);
}

// ---------------------------------------------------------------------------
// BF-21: VersionNumber normalization breaks string version comparison.
// VersionNumber::toString() strips trailing zeros ("1.0.0" -> "1.0"), so a
// plain string compare in findSnapshot/findSinger wrongly rejects "1.0.0"
// when the stored value is "1.0". Fixed by versionsMatch() using semantic
// VersionNumber comparison.
// ---------------------------------------------------------------------------

TEST_CASE("VoicebankScanner version normalization does not break matching", "[ds-bank][scanner][bf-21]") {
    const auto root = makeTempDir("bf21-version-norm");
    createPackage(root / "pkg_a", "pkg.a", "1.2.3");
    createPackage(root / "pkg_b", "pkg.b", "1.2.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({root});
    scanner.refresh();
    REQUIRE(scanner.singers().size() == 2);

    // "1.2.3" has non-zero patch, so toString() preserves it.
    SingerRef refA{"pkg.a", "test_singer", "1.2.3"};
    auto snapA = scanner.singerSnapshot(refA);
    REQUIRE(snapA.hasValue());
    REQUIRE(snapA->ref.packageId == "pkg.a");

    // "1.2.0" normalizes to "1.2" but querying "1.2.0" must still match.
    SingerRef refB{"pkg.b", "test_singer", "1.2.0"};
    auto snapB = scanner.singerSnapshot(refB);
    REQUIRE(snapB.hasValue());
    REQUIRE(snapB->ref.packageId == "pkg.b");

    // Equivalent forms should all match the same snapshot.
    SingerRef refB2{"pkg.b", "test_singer", "1.2"};
    auto snapB2 = scanner.singerSnapshot(refB2);
    REQUIRE(snapB2.hasValue());
    REQUIRE(snapB2->ref.packageId == "pkg.b");

    SingerRef refB3{"pkg.b", "test_singer", "1.2.0.0"};
    auto snapB3 = scanner.singerSnapshot(refB3);
    REQUIRE(snapB3.hasValue());
    REQUIRE(snapB3->ref.packageId == "pkg.b");

    // findSinger with normalized and non-normalized forms.
    auto r1 = scanner.findSinger("test_singer", "pkg.b", "1.2.0");
    REQUIRE(r1.hasValue());
    auto r2 = scanner.findSinger("test_singer", "pkg.b", "1.2");
    REQUIRE(r2.hasValue());
    auto r3 = scanner.findSinger("test_singer", "pkg.b", "1.2.0.0");
    REQUIRE(r3.hasValue());

    std::filesystem::remove_all(root);
}
