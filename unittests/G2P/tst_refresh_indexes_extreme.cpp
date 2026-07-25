// G2P refreshPackageIndexes extreme tests (BF-52 regression).
//
// Covers BF-52: PackageManager::Impl::refreshPackageIndexes (and the
// parallel enumeration in getModuleMetadatas) previously swallowed
// package.json parse / read failures via `if (!expObj) continue;` with
// no diagnostic. The fix (ROBUST-05) inserts an `srtWarning` log call
// before each `continue` so the operator can see WHY a package was
// skipped during index refresh.
//
// Fix sites (all in PackageManager.cpp):
//   - Impl::refreshPackageIndexes           (line ~282)
//   - PackageManager::getModuleMetadatas    (line ~1025)
//
// Testability note
// ----------------
// `refreshPackageIndexes` is a private Impl method, so it cannot be
// invoked directly. It is called from `getModuleMetadatas(ctxKey)` when
// `m_packagePathsDirty == true`, which is set by `addPackagePath(...)` /
// `setPackagePaths(...)`. Therefore all tests exercise the public
// `addPackagePath` + `getModuleMetadatas` pair, which transitively
// triggers refresh + enumeration.
//
// The `srtWarning` log calls themselves are not asserted directly here
// (capturing log output requires installing a global Logger callback
// which is process-wide state; doing so would race with other tests in
// the same binary). Instead, the OBSERVABLE postcondition is verified:
// invalid packages are skipped (not present in returned metadata),
// valid packages are still indexed, and no crash occurs on edge inputs.
// A future test could install a Logger callback under a serial tag to
// capture the warning text; this is left as an extension.
//
// Tag: [g2p][bf-52][extreme]
// Naming: BF-52-XXX

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Dependency/DependencyGraph.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/Core/PackageManager.h>
#include <synthrt/G2P/Support/Error.h>

using namespace srt::g2p;
using srt::core::ErrorCode;

namespace {

    std::filesystem::path makeTempDir(const std::string &name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = std::filesystem::temp_directory_path() /
                   ("srt-g2p-bf52-" + name + "-" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        return dir;
    }

    void writeFile(const std::filesystem::path &path, const std::string &text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << text;
    }

    /// Build a valid single-module manifest. Each module has packageId
    /// and version fields so it can be discovered by refreshPackageIndexes
    /// and enumerated by getModuleMetadatas.
    std::string validManifest(const std::string &pkgId, const std::string &version,
                              const std::string &moduleId = "m1",
                              const std::string &className = "srt.g2p.task.v1") {
        return R"({"packageId":")" + pkgId + R"(","version":")" + version +
               R"(","modules":{"g2p":[{"moduleId":")" + moduleId + R"(","class":")" +
               className + R"("}]}})";
    }

    /// Build a manifest with multiple categories (g2p + dict).
    std::string multiCategoryManifest(const std::string &pkgId, const std::string &version) {
        return R"({"packageId":")" + pkgId + R"(","version":")" + version +
               R"(","modules":{"g2p":[{"moduleId":"g1","class":"srt.g2p.task.v1"}],)"
               R"("dict":[{"moduleId":"d1","class":"srt.dict.task.v1"}]}})";
    }

    /// Helper: register the root directory as the default-context package
    /// path, then fetch module metadatas via the default context. The
    /// default context (empty name + empty version) is the simplest valid
    /// context and avoids the versioned-context plumbing.
    std::vector<srt::dependency::ModuleMetadata>
    refreshAndGet(PackageManager &pm, const std::filesystem::path &root) {
        auto addExp = pm.addPackagePath("", stdc::VersionNumber{}, root);
        REQUIRE(addExp.hasValue());
        return pm.getModuleMetadatas("");
    }

} // namespace

// ===========================================================================
// BF-52-001: refreshPackageIndexes skips malformed package.json.
//   PackageData::readDesc returns G2pConfigError for invalid JSON; the
//   fix logs srtWarning and continues to the next package. The metadata
//   result must be empty (no metadata from a package whose manifest
//   could not be parsed).
// ===========================================================================
TEST_CASE("BF-52-001: refreshPackageIndexes skips malformed package.json",
          "[g2p][bf-52][extreme]") {
    PackageManager pm;
    const auto root = makeTempDir("malformed");
    const auto pkgDir = root / "pkg";
    std::filesystem::create_directories(pkgDir);
    writeFile(pkgDir / "package.json", "{ this is : not valid json ]");

    auto metas = refreshAndGet(pm, root);
    REQUIRE(metas.empty());

    std::filesystem::remove_all(root);
}

// ===========================================================================
// BF-52-002: refreshPackageIndexes skips package missing packageId.
//   Valid JSON but no "packageId" field -> both refresh and metadata
//   enumeration skip silently (no srtWarning for this case — the fix
//   only logs for readDesc failures, not for missing-field skips).
// ===========================================================================
TEST_CASE("BF-52-002: refreshPackageIndexes skips missing packageId",
          "[g2p][bf-52][extreme]") {
    PackageManager pm;
    const auto root = makeTempDir("no-pkgid");
    const auto pkgDir = root / "pkg";
    std::filesystem::create_directories(pkgDir);
    writeFile(pkgDir / "package.json", R"({"version":"1.0.0","modules":{"g2p":[]}})");

    auto metas = refreshAndGet(pm, root);
    REQUIRE(metas.empty());

    std::filesystem::remove_all(root);
}

// ===========================================================================
// BF-52-003: refreshPackageIndexes skips package missing version.
// ===========================================================================
TEST_CASE("BF-52-003: refreshPackageIndexes skips missing version",
          "[g2p][bf-52][extreme]") {
    PackageManager pm;
    const auto root = makeTempDir("no-version");
    const auto pkgDir = root / "pkg";
    std::filesystem::create_directories(pkgDir);
    writeFile(pkgDir / "package.json",
              R"({"packageId":"p.no_ver","modules":{"g2p":[]}})");

    auto metas = refreshAndGet(pm, root);
    REQUIRE(metas.empty());

    std::filesystem::remove_all(root);
}

// ===========================================================================
// BF-52-004: refreshPackageIndexes skips package with empty version string.
//   "version": "" parses to an empty VersionNumber (isEmpty() == true) and
//   is skipped by both refreshPackageIndexes and the metadata enumerator.
// ===========================================================================
TEST_CASE("BF-52-004: refreshPackageIndexes skips empty version string",
          "[g2p][bf-52][extreme]") {
    PackageManager pm;
    const auto root = makeTempDir("empty-version");
    const auto pkgDir = root / "pkg";
    std::filesystem::create_directories(pkgDir);
    writeFile(pkgDir / "package.json",
              R"({"packageId":"p.empty_ver","version":"","modules":{"g2p":[]}})");

    auto metas = refreshAndGet(pm, root);
    REQUIRE(metas.empty());

    std::filesystem::remove_all(root);
}

// ===========================================================================
// BF-52-005: Mixed valid + invalid packages — only the valid one is indexed.
//   Context path contains:
//     - pkg_valid   : valid manifest with 1 g2p module
//     - pkg_malformed: malformed JSON
//     - pkg_noid     : missing packageId
//     - pkg_nover    : missing version
//   Only pkg_valid should appear in the metadata result.
// ===========================================================================
TEST_CASE("BF-52-005: mixed valid and invalid packages index only valid one",
          "[g2p][bf-52][extreme]") {
    PackageManager pm;
    const auto root = makeTempDir("mixed");

    const auto validDir = root / "pkg_valid";
    std::filesystem::create_directories(validDir);
    writeFile(validDir / "package.json", validManifest("p.valid", "1.0.0", "m_valid"));

    const auto malformedDir = root / "pkg_malformed";
    std::filesystem::create_directories(malformedDir);
    writeFile(malformedDir / "package.json", "{ broken json");

    const auto noidDir = root / "pkg_noid";
    std::filesystem::create_directories(noidDir);
    writeFile(noidDir / "package.json", R"({"version":"1.0.0","modules":{"g2p":[]}})");

    const auto noverDir = root / "pkg_nover";
    std::filesystem::create_directories(noverDir);
    writeFile(noverDir / "package.json",
              R"({"packageId":"p.nover","modules":{"g2p":[]}})");

    auto metas = refreshAndGet(pm, root);
    REQUIRE(metas.size() == 1);
    REQUIRE(metas[0].packageId == "p.valid");
    REQUIRE(metas[0].moduleId == "m_valid");
    REQUIRE(metas[0].type == "g2p");

    std::filesystem::remove_all(root);
}

// ===========================================================================
// BF-52-006: getModuleMetadatas on an unregistered context returns empty.
//   No addPackagePath call has been made for this context; the lookup in
//   m_contextPackagePaths misses and an empty vector is returned (no crash,
//   no exception).
// ===========================================================================
TEST_CASE("BF-52-006: getModuleMetadatas on unregistered context returns empty",
          "[g2p][bf-52][extreme]") {
    PackageManager pm;
    auto metas = pm.getModuleMetadatas("never_registered_ctx");
    REQUIRE(metas.empty());
}

// ===========================================================================
// BF-52-007: refreshPackageIndexes with empty context directory.
//   The context path itself is a valid empty directory (no subdirs). No
//   packages to enumerate; result is empty. No crash.
// ===========================================================================
TEST_CASE("BF-52-007: refreshPackageIndexes with empty context directory",
          "[g2p][bf-52][extreme]") {
    PackageManager pm;
    const auto root = makeTempDir("empty-root");
    // root is an empty directory — no subdirs, no package.json

    auto metas = refreshAndGet(pm, root);
    REQUIRE(metas.empty());

    std::filesystem::remove_all(root);
}

// ===========================================================================
// BF-52-008: refreshPackageIndexes skips subdir without package.json.
//   A subdir exists in the context path but has no package.json; it is
//   skipped silently. Other valid packages are still indexed.
// ===========================================================================
TEST_CASE("BF-52-008: refreshPackageIndexes skips subdir without package.json",
          "[g2p][bf-52][extreme]") {
    PackageManager pm;
    const auto root = makeTempDir("no-manifest");

    // Subdir without package.json
    const auto noManifestDir = root / "no_manifest";
    std::filesystem::create_directories(noManifestDir);

    // Valid package alongside it
    const auto validDir = root / "pkg_valid";
    std::filesystem::create_directories(validDir);
    writeFile(validDir / "package.json", validManifest("p.valid", "1.0.0", "m_valid"));

    auto metas = refreshAndGet(pm, root);
    REQUIRE(metas.size() == 1);
    REQUIRE(metas[0].packageId == "p.valid");

    std::filesystem::remove_all(root);
}

// ===========================================================================
// BF-52-009: refreshPackageIndexes skips non-directory entries in the
//   context path (stray files). No crash, no metadata returned for the
//   stray file.
// ===========================================================================
TEST_CASE("BF-52-009: refreshPackageIndexes skips non-directory entries",
          "[g2p][bf-52][extreme]") {
    PackageManager pm;
    const auto root = makeTempDir("stray-file");

    // A stray file (not a directory) in the context path.
    writeFile(root / "stray_file.txt", "not a directory");

    // A valid package alongside it.
    const auto validDir = root / "pkg_valid";
    std::filesystem::create_directories(validDir);
    writeFile(validDir / "package.json", validManifest("p.valid", "1.0.0", "m_valid"));

    auto metas = refreshAndGet(pm, root);
    REQUIRE(metas.size() == 1);
    REQUIRE(metas[0].packageId == "p.valid");

    std::filesystem::remove_all(root);
}

// ===========================================================================
// BF-52-010: getModuleMetadatas returns multiple modules from multiple
//   valid packages. Each package contributes one metadata entry.
// ===========================================================================
TEST_CASE("BF-52-010: getModuleMetadatas returns modules from multiple packages",
          "[g2p][bf-52][extreme]") {
    PackageManager pm;
    const auto root = makeTempDir("multi-pkg");

    const auto pkg1Dir = root / "pkg1";
    std::filesystem::create_directories(pkg1Dir);
    writeFile(pkg1Dir / "package.json", validManifest("p.one", "1.0.0", "m_one"));

    const auto pkg2Dir = root / "pkg2";
    std::filesystem::create_directories(pkg2Dir);
    writeFile(pkg2Dir / "package.json", validManifest("p.two", "2.0.0", "m_two"));

    auto metas = refreshAndGet(pm, root);
    REQUIRE(metas.size() == 2);

    // Verify both packages are represented (order is filesystem-dependent,
    // so check by set membership).
    bool foundOne = false, foundTwo = false;
    for (const auto &m : metas) {
        if (m.packageId == "p.one" && m.moduleId == "m_one")
            foundOne = true;
        if (m.packageId == "p.two" && m.moduleId == "m_two")
            foundTwo = true;
    }
    REQUIRE(foundOne);
    REQUIRE(foundTwo);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// BF-52-011: Multi-category package contributes one metadata per category.
//   A package with both "g2p" and "dict" modules yields two metadata
//   entries with different `type` fields.
// ===========================================================================
TEST_CASE("BF-52-011: multi-category package returns metadata per category",
          "[g2p][bf-52][extreme]") {
    PackageManager pm;
    const auto root = makeTempDir("multi-cat");
    const auto pkgDir = root / "pkg";
    std::filesystem::create_directories(pkgDir);
    writeFile(pkgDir / "package.json", multiCategoryManifest("p.multi", "1.0.0"));

    auto metas = refreshAndGet(pm, root);
    REQUIRE(metas.size() == 2);

    bool foundG2p = false, foundDict = false;
    for (const auto &m : metas) {
        if (m.type == "g2p" && m.moduleId == "g1")
            foundG2p = true;
        if (m.type == "dict" && m.moduleId == "d1")
            foundDict = true;
    }
    REQUIRE(foundG2p);
    REQUIRE(foundDict);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// BF-52-012: Module entry missing moduleId is skipped.
//   The package itself is valid (packageId + version present), but the
//   module entry has no "moduleId" field. The metadata enumerator skips
//   this entry; result is empty.
// ===========================================================================
TEST_CASE("BF-52-012: module entry missing moduleId is skipped",
          "[g2p][bf-52][extreme]") {
    PackageManager pm;
    const auto root = makeTempDir("no-modid");
    const auto pkgDir = root / "pkg";
    std::filesystem::create_directories(pkgDir);
    writeFile(pkgDir / "package.json",
              R"({"packageId":"p.no_modid","version":"1.0.0",)"
              R"("modules":{"g2p":[{"class":"srt.g2p.task.v1"}]}})");

    auto metas = refreshAndGet(pm, root);
    REQUIRE(metas.empty());

    std::filesystem::remove_all(root);
}

// ===========================================================================
// BF-52-013: Module entry missing class is skipped.
//   The module entry has "moduleId" but no "class" field. Skipped.
// ===========================================================================
TEST_CASE("BF-52-013: module entry missing class is skipped",
          "[g2p][bf-52][extreme]") {
    PackageManager pm;
    const auto root = makeTempDir("no-class");
    const auto pkgDir = root / "pkg";
    std::filesystem::create_directories(pkgDir);
    writeFile(pkgDir / "package.json",
              R"({"packageId":"p.no_class","version":"1.0.0",)"
              R"("modules":{"g2p":[{"moduleId":"m1"}]}})");

    auto metas = refreshAndGet(pm, root);
    REQUIRE(metas.empty());

    std::filesystem::remove_all(root);
}

// ===========================================================================
// BF-52-014: modules field of wrong type is treated as no modules.
//   "modules": [] / "modules": "string" / "modules": 123 — all are not
//   objects, so the metadata enumerator skips the package entirely.
// ===========================================================================
TEST_CASE("BF-52-014: modules field wrong type yields no metadata",
          "[g2p][bf-52][extreme]") {
    PackageManager pm;

    SECTION("modules is array") {
        const auto root = makeTempDir("mods-array");
        const auto pkgDir = root / "pkg";
        std::filesystem::create_directories(pkgDir);
        writeFile(pkgDir / "package.json",
                  R"({"packageId":"p.arr","version":"1.0.0","modules":[]})");
        auto metas = refreshAndGet(pm, root);
        REQUIRE(metas.empty());
        std::filesystem::remove_all(root);
    }

    SECTION("modules is string") {
        const auto root = makeTempDir("mods-string");
        const auto pkgDir = root / "pkg";
        std::filesystem::create_directories(pkgDir);
        writeFile(pkgDir / "package.json",
                  R"({"packageId":"p.str","version":"1.0.0","modules":"oops"})");
        auto metas = refreshAndGet(pm, root);
        REQUIRE(metas.empty());
        std::filesystem::remove_all(root);
    }

    SECTION("modules missing entirely") {
        const auto root = makeTempDir("mods-missing");
        const auto pkgDir = root / "pkg";
        std::filesystem::create_directories(pkgDir);
        writeFile(pkgDir / "package.json",
                  R"({"packageId":"p.miss","version":"1.0.0"})");
        auto metas = refreshAndGet(pm, root);
        REQUIRE(metas.empty());
        std::filesystem::remove_all(root);
    }
}

// ===========================================================================
// BF-52-015: Module entry that is not an object is skipped.
//   "modules": {"g2p": ["string_instead_of_object"]} — the array element
//   is a string, not an object. The enumerator's `moduleEntry.isObject()`
//   check skips it.
// ===========================================================================
TEST_CASE("BF-52-015: module entry not an object is skipped",
          "[g2p][bf-52][extreme]") {
    PackageManager pm;
    const auto root = makeTempDir("entry-not-obj");
    const auto pkgDir = root / "pkg";
    std::filesystem::create_directories(pkgDir);
    writeFile(pkgDir / "package.json",
              R"({"packageId":"p.entry","version":"1.0.0",)"
              R"("modules":{"g2p":["string_instead_of_object"]}})");

    auto metas = refreshAndGet(pm, root);
    REQUIRE(metas.empty());

    std::filesystem::remove_all(root);
}

// ===========================================================================
// BF-52-016: Category list value not an array is skipped.
//   "modules": {"g2p": {"obj_instead_of_array": ...}} — the category's
//   value is an object, not an array. The enumerator's `moduleListVal.isArray()`
//   check skips it.
// ===========================================================================
TEST_CASE("BF-52-016: category list not an array is skipped",
          "[g2p][bf-52][extreme]") {
    PackageManager pm;
    const auto root = makeTempDir("cat-not-arr");
    const auto pkgDir = root / "pkg";
    std::filesystem::create_directories(pkgDir);
    writeFile(pkgDir / "package.json",
              R"({"packageId":"p.cat","version":"1.0.0",)"
              R"("modules":{"g2p":{"obj_instead_of_array":true}}})");

    auto metas = refreshAndGet(pm, root);
    REQUIRE(metas.empty());

    std::filesystem::remove_all(root);
}

// ===========================================================================
// BF-52-017: packagePaths reflects registered context paths.
//   After addPackagePath succeeds, packagePaths returns the canonical
//   path. This guards the prerequisite for refreshPackageIndexes having
//   any input to scan.
// ===========================================================================
TEST_CASE("BF-52-017: addPackagePath registers path for refresh",
          "[g2p][bf-52][extreme]") {
    PackageManager pm;
    const auto root = makeTempDir("registered");
    const auto v0 = stdc::VersionNumber{};

    auto addExp = pm.addPackagePath("", v0, root);
    REQUIRE(addExp.hasValue());

    const auto paths = pm.packagePaths("");
    REQUIRE(paths.size() == 1);
    REQUIRE(std::filesystem::equivalent(paths[0], root));

    // Sanity: getModuleMetadatas on an empty path returns empty (no crash).
    auto metas = pm.getModuleMetadatas("");
    REQUIRE(metas.empty());

    std::filesystem::remove_all(root);
}
