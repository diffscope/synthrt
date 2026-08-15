// G2P PackageManager rollback extreme tests (BF-51 regression).
//
// Covers BF-51: PackageManager::Impl::openPackage / closePackage /
// closeAllLoadedPackages previously swallowed rollback errors via
// `std::ignore = cc->loadSpec(...);`. The fix (ROBUST-05) replaces each
// `std::ignore` with a logged `srtWarning` so rollback / unload failures
// are no longer silently lost.
//
// The fix sites are:
//   - Impl::openPackage: Initialized-phase rollback (Deleted) — line ~410
//   - Impl::openPackage: Ready-phase rollback (Finished then Deleted) — lines ~451/466
//   - Impl::closePackage: Finished + Deleted unload — lines ~542/553
//   - Impl::closeAllLoadedPackages: per-package close — line ~578
//
// Testability note
// ----------------
// The rollback paths are reached only when `ModuleCategory::loadSpec()`
// returns an Error during Initialized/Ready/Finished/Deleted transitions.
// The base `srt::core::ModuleCategory::loadSpec` implementation
// (Module.cpp:265 -> loadSpecBase) only changes spec state and never
// returns an Error, so the rollback paths cannot be triggered through the
// public `PackageManager::open()` API without a custom ModuleCategory
// override.
//
// What IS observable via the public API is the *postcondition* that the
// fix preserves: a failed open / duplicate open / mixed-state PackageManager
// must leave the loaded-package map consistent (no leaked entries, no
// double insertion, no residual pending state, no crash on destruction).
// These tests verify those postconditions, which are exactly the
// invariants the BF-51 fix must not regress.
//
// Tag: [g2p][bf-51][extreme]
// Naming: BF-51-XXX

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/Core/PackageManager.h>
#include <synthrt/G2P/Package/Package.h>
#include <synthrt/G2P/Support/Error.h>

using namespace srt::g2p;
using srt::core::ErrorCode;

namespace srt::core {
// Catch2 needs operator<< to stream ErrorCode values in failed assertions.
inline std::ostream &operator<<(std::ostream &os, const ErrorCode &code) {
    return os << errorCodeToString(code);
}
} // namespace srt::core

namespace {

    std::filesystem::path makeTempDir(const std::string &name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = std::filesystem::temp_directory_path() /
                   ("srt-g2p-bf51-" + name + "-" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        return dir;
    }

    void writeFile(const std::filesystem::path &path, const std::string &text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << text;
    }

    /// Minimal valid manifest used as a starting point. Caller may post-edit
    /// fields (packageId / version / modules) per case. The packageId is
    /// unique per call so that multiple SECTIONs in the same TEST_CASE do
    /// not collide in the singleton PackageManager state (each PackageManager
    /// is local here, but uniqueness keeps the test self-describing).
    std::string validManifest(const std::string &pkgId, const std::string &version) {
        return R"({"packageId":")" + pkgId + R"(","version":")" + version +
               R"(","modules":{"g2p":[{"moduleId":"m1","class":"srt.g2p.task.v1"}]}})";
    }

} // namespace

// ===========================================================================
// BF-51-001: Parse failure (malformed JSON) returns G2pConfigError and
// leaves no residual package in the loaded map.
// ===========================================================================
TEST_CASE("BF-51-001: malformed package.json leaves no residual", "[g2p][bf-51][extreme]") {
    PackageManager pm;
    const auto dir = makeTempDir("malformed");
    writeFile(dir / "package.json", "{ this is : not valid json ]");

    SECTION("open returns G2pConfigError") {
        auto exp = pm.open(dir);
        REQUIRE(!exp.hasValue());
        REQUIRE(exp.takeError().code() == ErrorCode::G2pConfigError);
    }

    SECTION("no residual package in loaded map") {
        (void)pm.open(dir);
        REQUIRE(pm.packages().empty());
        const auto v0 = stdc::VersionNumber{};
        REQUIRE(!pm.find("anything", v0).isValid());
    }

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// BF-51-002: Parse failure (missing required field) returns G2pConfigError.
//   Covers missing packageId, missing version, and empty version string.
// ===========================================================================
TEST_CASE("BF-51-002: missing required field returns G2pConfigError",
          "[g2p][bf-51][extreme]") {
    PackageManager pm;

    SECTION("missing packageId") {
        const auto dir = makeTempDir("no-pkgid");
        writeFile(dir / "package.json", R"({"version":"1.0.0"})");
        auto exp = pm.open(dir);
        REQUIRE(!exp.hasValue());
        REQUIRE(exp.takeError().code() == ErrorCode::G2pConfigError);
        REQUIRE(pm.packages().empty());
        std::filesystem::remove_all(dir);
    }

    SECTION("missing version") {
        const auto dir = makeTempDir("no-version");
        writeFile(dir / "package.json", R"({"packageId":"p.no_ver"})");
        auto exp = pm.open(dir);
        REQUIRE(!exp.hasValue());
        REQUIRE(exp.takeError().code() == ErrorCode::G2pConfigError);
        REQUIRE(pm.packages().empty());
        std::filesystem::remove_all(dir);
    }

    SECTION("empty version string") {
        const auto dir = makeTempDir("empty-version");
        writeFile(dir / "package.json", R"({"packageId":"p.empty_ver","version":""})");
        auto exp = pm.open(dir);
        REQUIRE(!exp.hasValue());
        REQUIRE(exp.takeError().code() == ErrorCode::G2pConfigError);
        REQUIRE(pm.packages().empty());
        std::filesystem::remove_all(dir);
    }
}

// ===========================================================================
// BF-51-003: Unknown module category returns G2pNotImplementedError.
//   Package.cpp parse() rejects categories not registered in the manager
//   (g2p/dict/driver). The error propagates via openPackage's parse step
//   (before Initialized/Ready rollback path) — but the *postcondition*
//   tested here is identical to a rollback-cleanup scenario: no residual.
// ===========================================================================
TEST_CASE("BF-51-003: unknown module category returns G2pNotImplementedError",
          "[g2p][bf-51][extreme]") {
    PackageManager pm;
    const auto dir = makeTempDir("unknown-cat");
    writeFile(dir / "package.json",
              R"({"packageId":"p.unknown_cat","version":"1.0.0",)"
              R"("modules":{"nonexistent_category":[{"moduleId":"m1","class":"c"}]}})");

    auto exp = pm.open(dir);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.takeError().code() == ErrorCode::G2pNotImplementedError);
    REQUIRE(pm.packages().empty());

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// BF-51-004: modules field with wrong type is treated as no modules.
//   Package.cpp parse() accepts missing or non-object modules as a valid
//   package with zero modules (m_loaded=true). This guards the rollback
//   invariant: a successful-but-empty open does not leak partial state.
// ===========================================================================
TEST_CASE("BF-51-004: modules field wrong type loads as empty package",
          "[g2p][bf-51][extreme]") {
    PackageManager pm;
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();

    SECTION("modules is array") {
        const auto dir = makeTempDir("mods-array");
        writeFile(dir / "package.json",
                  R"({"packageId":"p.mods_array","version":"1.0.0","modules":[]})");
        auto exp = pm.open(dir);
        REQUIRE(exp.hasValue());
        auto pkg = exp.take();
        REQUIRE(pkg.isValid());
        REQUIRE(pkg.isLoaded());
        REQUIRE(pkg.moduleSpecs("g2p").empty());
        // find reflects the loaded package
        auto found = pm.find("p.mods_array", v1);
        REQUIRE(found.isValid());
        REQUIRE(found.version() == v1);
        std::filesystem::remove_all(dir);
    }

    SECTION("modules is string") {
        const auto dir = makeTempDir("mods-string");
        writeFile(dir / "package.json",
                  R"({"packageId":"p.mods_str","version":"1.0.0","modules":"oops"})");
        auto exp = pm.open(dir);
        REQUIRE(exp.hasValue());
        auto pkg = exp.take();
        REQUIRE(pkg.isLoaded());
        REQUIRE(pkg.moduleSpecs("g2p").empty());
        std::filesystem::remove_all(dir);
    }

    SECTION("modules missing entirely") {
        const auto dir = makeTempDir("mods-missing");
        writeFile(dir / "package.json",
                  R"({"packageId":"p.mods_missing","version":"1.0.0"})");
        auto exp = pm.open(dir);
        REQUIRE(exp.hasValue());
        auto pkg = exp.take();
        REQUIRE(pkg.isLoaded());
        REQUIRE(pkg.moduleSpecs("g2p").empty());
        std::filesystem::remove_all(dir);
    }
}

// ===========================================================================
// BF-51-005: Re-opening the same path increments ref count, does not
// duplicate in the loaded map.
//   openPackage short-circuits when canonicalPath is already in
//   pathIndexes: `pkg.ref++` and returns the existing spec. The loaded
//   map must contain exactly one entry for the path even after multiple
//   opens.
// ===========================================================================
TEST_CASE("BF-51-005: re-open same path increments ref count without duplication",
          "[g2p][bf-51][extreme]") {
    PackageManager pm;
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();
    const auto dir = makeTempDir("reopen");
    writeFile(dir / "package.json", validManifest("p.reopen", "1.0.0"));

    auto exp1 = pm.open(dir);
    REQUIRE(exp1.hasValue());
    auto pkg1 = exp1.take();
    REQUIRE(pkg1.isValid());
    REQUIRE(pkg1.isLoaded());

    auto exp2 = pm.open(dir);
    REQUIRE(exp2.hasValue());
    auto pkg2 = exp2.take();
    REQUIRE(pkg2.isValid());
    REQUIRE(pkg2.isLoaded());

    // Same underlying package (id + version match, single entry in map).
    REQUIRE(pkg1.id() == pkg2.id());
    REQUIRE(pkg1.version() == pkg2.version());
    REQUIRE(pkg1.version() == v1);

    // Loaded map has exactly one package (ref count incremented, not duplicated).
    REQUIRE(pm.packages().size() == 1);
    auto found = pm.find("p.reopen", v1);
    REQUIRE(found.isValid());
    REQUIRE(found.version() == v1);

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// BF-51-006: Duplicate (same id+version) from a different path returns a
// Package with error set and isLoaded=false; the loaded map keeps only
// the first package. The duplicate is recorded in m_resourcePackages so
// PackageManager::~Impl can clean it up.
// ===========================================================================
TEST_CASE("BF-51-006: duplicate package from different path is recorded as failed",
          "[g2p][bf-51][extreme]") {
    PackageManager pm;
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();
    const auto dir1 = makeTempDir("dup-first");
    const auto dir2 = makeTempDir("dup-second");
    writeFile(dir1 / "package.json", validManifest("p.dup", "1.0.0"));
    writeFile(dir2 / "package.json", validManifest("p.dup", "1.0.0"));

    // First open succeeds. A successfully-loaded package has the default
    // (G2pSuccess) error; error().ok() is true.
    auto exp1 = pm.open(dir1);
    REQUIRE(exp1.hasValue());
    auto pkg1 = exp1.take();
    REQUIRE(pkg1.isLoaded());
    REQUIRE(pkg1.error().ok());

    // Second open of the same (id, version) at a different path returns a
    // Package handle. Note: PackageData::parse() sets m_loaded=true BEFORE
    // openPackage's duplicate check runs, so the duplicate also reports
    // isLoaded()==true. The duplicate is identified by m_err
    // (G2pFileSystemError "duplicate package ..."), NOT by isLoaded().
    // The duplicate is recorded in m_resourcePackages (NOT m_loadedPackageMap),
    // so it does not appear in packages() / find() results.
    auto exp2 = pm.open(dir2);
    REQUIRE(exp2.hasValue()); // not an Expected error — a Package handle
    auto pkg2 = exp2.take();
    REQUIRE(pkg2.isValid()); // m_mgr is set, so handle is non-empty
    REQUIRE(pkg2.isLoaded()); // parse() set m_loaded before duplicate check
    REQUIRE(!pkg2.error().ok());
    REQUIRE(pkg2.error().code() == ErrorCode::G2pFileSystemError);

    // Loaded map has exactly one package (the first one). find returns the
    // first one by version, and that one has no error.
    REQUIRE(pm.packages().size() == 1);
    auto found = pm.find("p.dup", v1);
    REQUIRE(found.isValid());
    REQUIRE(found.isLoaded());
    REQUIRE(found.error().ok());

    std::filesystem::remove_all(dir1);
    std::filesystem::remove_all(dir2);
}

// ===========================================================================
// BF-51-007: After a failed open (parse failure), the same path can be
// re-opened successfully once the manifest is fixed. This guards against
// residual m_pendingPackages state from the failed attempt leaking into
// the retry.
// ===========================================================================
TEST_CASE("BF-51-007: failed open does not block re-open after manifest fix",
          "[g2p][bf-51][extreme]") {
    PackageManager pm;
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();
    const auto dir = makeTempDir("retry");

    // 1. Malformed manifest -> open fails (G2pConfigError), no residual.
    writeFile(dir / "package.json", "{ broken json");
    auto exp1 = pm.open(dir);
    REQUIRE(!exp1.hasValue());
    REQUIRE(exp1.takeError().code() == ErrorCode::G2pConfigError);
    REQUIRE(pm.packages().empty());

    // 2. Fix the manifest -> open succeeds, package loads.
    writeFile(dir / "package.json", validManifest("p.retry", "1.0.0"));
    auto exp2 = pm.open(dir);
    REQUIRE(exp2.hasValue());
    auto pkg = exp2.take();
    REQUIRE(pkg.isLoaded());
    REQUIRE(pkg.id() == "p.retry");
    REQUIRE(pkg.version() == v1);

    // 3. find reflects the newly-loaded package.
    auto found = pm.find("p.retry", v1);
    REQUIRE(found.isValid());
    REQUIRE(found.version() == v1);

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// BF-51-008: Multiple sequential failures on the same path do not
// accumulate state. Each failed open is fully cleaned up before the next.
// ===========================================================================
TEST_CASE("BF-51-008: repeated failures on same path do not accumulate",
          "[g2p][bf-51][extreme]") {
    PackageManager pm;
    const auto dir = makeTempDir("repeat-fail");
    writeFile(dir / "package.json", "{ broken json");

    for (int i = 0; i < 3; ++i) {
        auto exp = pm.open(dir);
        REQUIRE(!exp.hasValue());
        REQUIRE(exp.takeError().code() == ErrorCode::G2pConfigError);
        REQUIRE(pm.packages().empty());
    }

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// BF-51-009: open() with invalid path arguments is rejected with
//   G2pFileSystemError without crashing.
// ===========================================================================
TEST_CASE("BF-51-009: open rejects invalid path arguments", "[g2p][bf-51][extreme]") {
    PackageManager pm;

    SECTION("empty path") {
        auto exp = pm.open(std::filesystem::path{});
        REQUIRE(!exp.hasValue());
        REQUIRE(exp.takeError().code() == ErrorCode::G2pFileSystemError);
    }

    SECTION("path is a file, not a directory") {
        const auto file = makeTempDir("notdir") / "package.json";
        writeFile(file, "not a directory");
        auto exp = pm.open(file);
        REQUIRE(!exp.hasValue());
        REQUIRE(exp.takeError().code() == ErrorCode::G2pFileSystemError);
        std::filesystem::remove_all(file.parent_path());
    }

    SECTION("nonexistent path") {
        const auto dir = std::filesystem::temp_directory_path() /
                         ("srt-g2p-bf51-nope-" +
                          std::to_string(
                              std::chrono::steady_clock::now().time_since_epoch().count()));
        REQUIRE(!std::filesystem::exists(dir));
        auto exp = pm.open(dir);
        REQUIRE(!exp.hasValue());
        REQUIRE(exp.takeError().code() == ErrorCode::G2pFileSystemError);
    }
}

// ===========================================================================
// BF-51-010: PackageManager destructor cleans up a mixed state of loaded,
//   failed-duplicate, and parse-failed packages without crashing.
//
//   This exercises the closeAllLoadedPackages + m_resourcePackages cleanup
//   path in Impl::~Impl, which is one of the BF-51 fix sites. A sanitizer
//   (ASan/LSan/UBSan) build would surface any leak / use-after-free here.
// ===========================================================================
TEST_CASE("BF-51-010: destructor handles mixed loaded/failed/duplicate state",
          "[g2p][bf-51][extreme]") {
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();

    // Use a scope so the PackageManager destructor runs at end of scope.
    {
        PackageManager pm;

        // 1. A valid loaded package.
        const auto dir1 = makeTempDir("mix-valid");
        writeFile(dir1 / "package.json", validManifest("p.mix_valid", "1.0.0"));
        auto exp1 = pm.open(dir1);
        REQUIRE(exp1.hasValue());
        REQUIRE(exp1.value().isLoaded());

        // 2. A duplicate of (1) at a different path -> recorded in
        //    m_resourcePackages (parse() set m_loaded=true before the
        //    duplicate check, so isLoaded()==true; the duplicate is
        //    identified by error().code()==G2pFileSystemError).
        const auto dir2 = makeTempDir("mix-dup");
        writeFile(dir2 / "package.json", validManifest("p.mix_valid", "1.0.0"));
        auto exp2 = pm.open(dir2);
        REQUIRE(exp2.hasValue());
        REQUIRE(exp2.value().isLoaded());
        REQUIRE(!exp2.value().error().ok());
        REQUIRE(exp2.value().error().code() == ErrorCode::G2pFileSystemError);

        // 3. A malformed package -> open fails, pd is deleted immediately.
        const auto dir3 = makeTempDir("mix-malformed");
        writeFile(dir3 / "package.json", "{ broken");
        auto exp3 = pm.open(dir3);
        REQUIRE(!exp3.hasValue());

        // 4. Another valid loaded package with a different id.
        const auto dir4 = makeTempDir("mix-other");
        writeFile(dir4 / "package.json", validManifest("p.mix_other", "1.0.0"));
        auto exp4 = pm.open(dir4);
        REQUIRE(exp4.hasValue());
        REQUIRE(exp4.value().isLoaded());

        // Loaded map has exactly the two unique (id, version) packages.
        REQUIRE(pm.packages().size() == 2);

        // Cleanup happens at end of scope via PackageManager destructor.
        // No REQUIRE beyond "no crash"; if execution reaches the end of the
        // scope, the destructor succeeded.
        std::filesystem::remove_all(dir1);
        std::filesystem::remove_all(dir2);
        std::filesystem::remove_all(dir3);
        std::filesystem::remove_all(dir4);
    }
    // If we reach this point, the destructor ran without crashing.
    REQUIRE(true);
}

// ===========================================================================
// BF-51-011: Open the same duplicate (id, version) twice in a row at the
//   same path. The first open succeeds; the second is a refcount increment
//   (path already in pathIndexes), NOT a duplicate-detection error.
//   This guards against the duplicate-detection logic misfiring on a
//   benign re-open of the same canonical path.
// ===========================================================================
TEST_CASE("BF-51-011: re-open of duplicate at SAME path is refcount, not duplicate error",
          "[g2p][bf-51][extreme]") {
    PackageManager pm;
    const auto dir = makeTempDir("same-path-reopen");
    writeFile(dir / "package.json", validManifest("p.same_reopen", "1.0.0"));

    auto exp1 = pm.open(dir);
    REQUIRE(exp1.hasValue());
    REQUIRE(exp1.value().isLoaded());
    REQUIRE(exp1.value().error().ok());

    auto exp2 = pm.open(dir);
    REQUIRE(exp2.hasValue());
    REQUIRE(exp2.value().isLoaded());
    // Refcount path: error must remain ok (no duplicate error set).
    REQUIRE(exp2.value().error().ok());

    REQUIRE(pm.packages().size() == 1);

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// BF-51-012: open() of two distinct packages (different ids) at different
//   paths produces two loaded entries; find returns each by id+version.
//   Guards the multi-package invariant in the loaded map.
// ===========================================================================
TEST_CASE("BF-51-012: two distinct packages coexist in loaded map",
          "[g2p][bf-51][extreme]") {
    PackageManager pm;
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();

    const auto dir1 = makeTempDir("coexist-a");
    const auto dir2 = makeTempDir("coexist-b");
    writeFile(dir1 / "package.json", validManifest("p.coexist_a", "1.0.0"));
    writeFile(dir2 / "package.json", validManifest("p.coexist_b", "1.0.0"));

    REQUIRE(pm.open(dir1).hasValue());
    REQUIRE(pm.open(dir2).hasValue());

    REQUIRE(pm.packages().size() == 2);
    REQUIRE(pm.find("p.coexist_a", v1).isValid());
    REQUIRE(pm.find("p.coexist_b", v1).isValid());

    std::filesystem::remove_all(dir1);
    std::filesystem::remove_all(dir2);
}
