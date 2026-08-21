// lib/Core edge condition test cases (CORE-001 ~ CORE-013)
//
// Cover Runtime / PluginFactory / ModuleLocator / ModuleCategory under abnormal input
// and concurrency scenarios. See test matrix in task description, corresponding to
// docs/refactoring-vnext/03-conventions.md §2.1 namespace convention (srt::core).
//
// === API difference notes from test matrix ===
// The actual return values of the following cases differ from the matrix description
// (adjusted per actual API):
//   - CORE-002: Matrix expected Error(FileNotFound); actual scanPackages for nonexistent
//     directory returns ErrorCode::PackageRootInvalid (fs::is_directory check before
//     canonical), see lib/Core/Core/Runtime.cpp:83-90.
//   - CORE-004: Matrix expected Error(AlreadyInitialized); actual initialize() second
//     call returns Error::InvalidArgument (no dedicated AlreadyInitialized type),
//     see lib/Core/Core/Runtime.cpp:621-638.
//   - CORE-005: Matrix expected Error(NotInitialized); actual loadPackage does not verify
//     initialization state, returns Error::FileNotFound for nonexistent package path
//     (desc.json missing). See lib/Core/Core/Runtime.cpp:122-147.
//   - CORE-006: Matrix expected Error(NotFound); actual unloadPackage for unloaded path
//     returns ErrorCode::RuntimePackageNotLoaded,
//     see lib/Core/Core/Runtime.cpp:533-537.
//   - CORE-007: Complete load→unload→load requires a real voicebank package
//     (desc.json + inference/singer configs), beyond unit test fixture scope,
//     documented without a test shell.
//   - CORE-013: Overly long path (>260 chars) is safely rejected by scanPackages
//     (PackageRootInvalid/FileNotOpen); verified directly with a non-existent
//     long path (no real long-path fixture required).

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/Core/Dependency/DependencyResolver.h>
#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Plugin/PluginFactory.h>
#include <synthrt/Core/Support/ConfigAccessor.h>
#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>

using namespace srt::core;

namespace srt::core {
// Catch2 needs operator<< to stream ErrorCode values in failed assertions.
inline std::ostream &operator<<(std::ostream &os, const ErrorCode &code) {
    return os << errorCodeToString(code);
}
} // namespace srt::core

namespace {

    // Construct a unique temporary directory as package root; caller is responsible for cleanup.
    std::filesystem::path makeTempRoot(const std::string &name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = std::filesystem::temp_directory_path() /
                   ("srt-core-edge-" + name + "-" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        return dir;
    }

} // namespace

// ---------------------------------------------------------------------------
// CORE-001/002/003: scanPackages rejects invalid paths
// Merged: all three cases share the Runtime construction and verify path
// rejection. SECTION form keeps per-input traceability while removing the
// triplicated Runtime setup. Adds a relative-path-that-exists boundary case.
// ---------------------------------------------------------------------------
TEST_CASE("CORE-001/002/003: scanPackages rejects invalid paths", "[core][edge]") {
    Runtime runtime;

    SECTION("CORE-001: empty path -> PackageRootInvalid") {
        auto result = runtime.scanPackages(std::filesystem::path{});
        REQUIRE_FALSE(result.hasValue());
        REQUIRE(!result.error().ok());
        // Empty path hits PackageRootInvalid check, does not crash
        REQUIRE(result.error().code() == ErrorCode::PackageRootInvalid);
    }

    SECTION("CORE-002: nonexistent directory -> PackageRootInvalid") {
        // API difference: actual returns PackageRootInvalid (not FileNotFound)
        auto result = runtime.scanPackages("/nonexistent/path/does-not-exist");
        REQUIRE_FALSE(result.hasValue());
        REQUIRE(!result.error().ok());
        REQUIRE(result.error().code() == ErrorCode::PackageRootInvalid);
    }

    SECTION("CORE-003: path traversal is safely rejected") {
        // Path traversal string does not point to a real directory on Windows,
        // fs::is_directory returns false.
        auto result = runtime.scanPackages("../../../etc/passwd");
        REQUIRE_FALSE(result.hasValue());
        // Does not crash and is safely rejected (PackageRootInvalid or
        // FileNotOpen both acceptable).
        const auto code = result.error().code();
        REQUIRE((code == ErrorCode::PackageRootInvalid ||
                 code == ErrorCode::FileNotOpen));
    }

    SECTION("CORE-003b: file path (not directory) is rejected") {
        // Boundary: a path that exists but is a file (not a directory) must
        // also be rejected by the is_directory guard.
        const auto tmpFile = makeTempRoot("core-file-not-dir") / "not-a-dir.txt";
        { std::ofstream f(tmpFile); f << "x"; }
        auto result = runtime.scanPackages(tmpFile);
        REQUIRE_FALSE(result.hasValue());
        REQUIRE(result.error().code() == ErrorCode::PackageRootInvalid);
        std::filesystem::remove_all(tmpFile.parent_path());
    }
}

// ---------------------------------------------------------------------------
// CORE-004: initialize hard idempotency
// API difference: second initialize returns Error::InvalidArgument (not dedicated AlreadyInitialized)
// ---------------------------------------------------------------------------
TEST_CASE("CORE-004: initialize is hard-idempotent", "[core][edge]") {
    Runtime runtime;
    REQUIRE(runtime.initialize().hasValue());
    REQUIRE(runtime.isInitialized());

    auto second = runtime.initialize();
    REQUIRE_FALSE(second.hasValue());
    REQUIRE(second.error().type() == Error::InvalidArgument);
    REQUIRE_FALSE(second.error().message().empty());
}

// ---------------------------------------------------------------------------
// CORE-005/006: loadPackage / unloadPackage error paths
// Merged: both verify package-path rejection on an uninitialized Runtime and
// share the Runtime construction. SECTION form preserves the per-API
// traceability while removing the duplicate setup.
// ---------------------------------------------------------------------------
TEST_CASE("CORE-005/006: loadPackage/unloadPackage error paths", "[core][edge]") {
    Runtime runtime;
    REQUIRE_FALSE(runtime.isInitialized());

    SECTION("CORE-005: loadPackage nonexistent path -> FileNotFound") {
        // API difference: loadPackage does not verify initialization state,
        // returns FileNotFound for nonexistent package (desc.json missing).
        auto result = runtime.loadPackage("/nonexistent/package/path");
        REQUIRE_FALSE(result.hasValue());
        REQUIRE(result.error().type() == Error::FileNotFound);
    }

    SECTION("CORE-006: unloadPackage unloaded path -> RuntimePackageNotLoaded") {
        // API difference: returns ErrorCode::RuntimePackageNotLoaded (not
        // generic NotFound).
        auto result = runtime.unloadPackage("/not/loaded/path");
        REQUIRE_FALSE(result.hasValue());
        REQUIRE(result.error().code() == ErrorCode::RuntimePackageNotLoaded);
    }

    SECTION("CORE-006b: unloadPackage empty path -> RuntimePackageNotLoaded") {
        // Boundary: empty path must also be rejected (not crash, not succeed).
        auto result = runtime.unloadPackage(std::filesystem::path{});
        REQUIRE_FALSE(result.hasValue());
        REQUIRE(result.error().code() == ErrorCode::RuntimePackageNotLoaded);
    }
}

// ---------------------------------------------------------------------------
// CORE-008: moduleCategory with unknown name
// ---------------------------------------------------------------------------
TEST_CASE("CORE-008: moduleCategory returns nullptr for unknown name", "[core][edge]") {
    Runtime runtime;
    REQUIRE(runtime.moduleCategory("nonexistent") == nullptr);
}

// ---------------------------------------------------------------------------
// CORE-009: multithreaded concurrent scanPackages on different directories
// Use two independent Runtime instances to scan different roots, verify no data race/crash
// ---------------------------------------------------------------------------
TEST_CASE("CORE-009: concurrent scanPackages on different roots", "[core][edge]") {
    const auto root1 = makeTempRoot("concurrent-a");
    const auto root2 = makeTempRoot("concurrent-b");
    // Create a sub-package directory placeholder under each root
    std::filesystem::create_directories(root1 / "pkg-a");
    std::filesystem::create_directories(root2 / "pkg-b");

    Runtime rt1, rt2;
    // Default construction is success; Expected<void> forbids construction from success Error
    Expected<void> r1;
    Expected<void> r2;

    auto t1 = std::thread([&] { r1 = rt1.scanPackages(root1); });
    auto t2 = std::thread([&] { r2 = rt2.scanPackages(root2); });
    t1.join();
    t2.join();

    // Both threads should succeed (independent instances scanning valid directories), no crash
    REQUIRE(r1.hasValue());
    REQUIRE(r2.hasValue());
    REQUIRE(rt1.discoveredPackages().size() == 1);
    REQUIRE(rt2.discoveredPackages().size() == 1);

    std::filesystem::remove_all(root1);
    std::filesystem::remove_all(root2);
}

// ---------------------------------------------------------------------------
// CORE-010: PluginFactory addPluginPath with empty iid
// addPluginPath returns void, empty iid as map key is valid, should not crash
// ---------------------------------------------------------------------------
TEST_CASE("CORE-010: PluginFactory addPluginPath tolerates empty iid", "[core][edge]") {
    PluginFactory factory;
    const auto tmp = makeTempRoot("empty-iid");
    // Empty iid + temporary directory: should not crash, should not throw
    REQUIRE_NOTHROW(factory.addPluginPath("", tmp));
    REQUIRE_NOTHROW(factory.addPluginPath("", std::filesystem::path{}));
    // pluginPaths("") should return empty or previously registered paths, should not crash
    REQUIRE_NOTHROW(factory.pluginPaths(""));
    std::filesystem::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// CORE-011: PluginFactory plugin lookup with nonexistent key
// ---------------------------------------------------------------------------
TEST_CASE("CORE-011: PluginFactory plugin returns nullptr for missing key",
          "[core][edge]") {
    PluginFactory factory;
    REQUIRE(factory.plugin("some.iid", "nonexistent") == nullptr);
}

// ---------------------------------------------------------------------------
// CORE-012: ModuleLocator fromString with malformed string
// ':::invalid:::' contains invalid char ':', fromString returns empty locator
// ---------------------------------------------------------------------------
TEST_CASE("CORE-012: ModuleLocator fromString rejects malformed token", "[core][edge]") {
    REQUIRE_FALSE(ModuleLocator::isValidLocator(":::invalid:::"));

    auto loc = ModuleLocator::fromString(":::invalid:::");
    REQUIRE(loc.isEmpty());
    REQUIRE(loc.package().empty());
    REQUIRE(loc.id().empty());
}

// ---------------------------------------------------------------------------
// CORE-013: Overly long path (>260 chars Windows)
// Windows MAX_PATH (260) limits canonical paths without the \\?\ prefix.
// scanPackages (Runtime.cpp:74-120) guards with fs::is_directory and wraps
// fs::canonical/directory_iterator in try-catch, so a >260-char path must be
// rejected safely (PackageRootInvalid from the is_directory guard, or
// FileNotOpen if canonical throws) rather than crashing. Verified with a
// non-existent long path — no real long-path fixture required.
// ---------------------------------------------------------------------------
TEST_CASE("CORE-013: scanPackages with very long path", "[core][edge]") {
    Runtime runtime;
    // Build a non-existent path exceeding Windows MAX_PATH (260 chars).
    const std::string longComponent(260, 'a');
    const std::filesystem::path longPath =
        std::filesystem::path("/nonexistent") / longComponent;
    REQUIRE(longPath.string().size() > 260);

    auto result = runtime.scanPackages(longPath);
    REQUIRE_FALSE(result.hasValue());
    REQUIRE(!result.error().ok());
    // Long non-existent path is safely rejected (PackageRootInvalid from the
    // is_directory guard, or FileNotOpen if canonical throws); no crash.
    const auto code = result.error().code();
    REQUIRE((code == ErrorCode::PackageRootInvalid || code == ErrorCode::FileNotOpen));
}

// ===========================================================================
// CORE-014 ~ CORE-022: Supplementary ModuleLocator and ModuleSpec::State edge coverage.
// Only uses Module.h public API (fromString/toString/isEmpty/isValidLocator/
// operator==/State enum), no dependency on Runtime instance or filesystem fixture.
// ===========================================================================

// ---------------------------------------------------------------------------
// CORE-014: ModuleLocator default constructor creates empty locator
// isEmpty() depends only on whether _id is empty, so default-constructed locator isEmpty() is true.
// ---------------------------------------------------------------------------
TEST_CASE("CORE-014: ModuleLocator default constructor creates empty locator",
          "[core][edge]") {
    ModuleLocator loc;
    REQUIRE(loc.isEmpty());
    REQUIRE(loc.package().empty());
    REQUIRE(loc.id().empty());
    REQUIRE(loc.version().isEmpty());
}

// ---------------------------------------------------------------------------
// CORE-015: ModuleLocator::fromString("") returns empty locator
// Explicitly calling fromString with empty string should return empty locator, no crash.
// ---------------------------------------------------------------------------
TEST_CASE("CORE-015: ModuleLocator fromString rejects empty string", "[core][edge]") {
    auto loc = ModuleLocator::fromString("");
    REQUIRE(loc.isEmpty());
    REQUIRE(loc.id().empty());
    REQUIRE(loc.package().empty());
}

// ---------------------------------------------------------------------------
// CORE-016: ModuleLocator::fromString parses simple id
// String without '/' separator is used directly as id; package name and version are empty.
// ---------------------------------------------------------------------------
TEST_CASE("CORE-016: ModuleLocator fromString parses simple id", "[core][edge]") {
    const std::string token = "my-module-id";
    REQUIRE(ModuleLocator::isValidLocator(token));

    auto loc = ModuleLocator::fromString(token);
    REQUIRE_FALSE(loc.isEmpty());
    REQUIRE(loc.id() == token);
    REQUIRE(loc.package().empty());
    REQUIRE(loc.version().isEmpty());
}

// ---------------------------------------------------------------------------
// CORE-017: ModuleLocator::fromString parses package/id format
// "pkg/my-id" format: leftPart is package, rightPart is id.
// ---------------------------------------------------------------------------
TEST_CASE("CORE-017: ModuleLocator fromString parses package/id format",
          "[core][edge]") {
    const std::string token = "my-package/my-module";
    // Note: isValidLocator validates a single token (package name or id),
    // not a full locator path containing '/'. Skip isValidLocator here.
    auto loc = ModuleLocator::fromString(token);
    REQUIRE_FALSE(loc.isEmpty());
    REQUIRE(loc.package() == "my-package");
    REQUIRE(loc.id() == "my-module");
    REQUIRE(loc.version().isEmpty());
}

// ---------------------------------------------------------------------------
// CORE-018: ModuleLocator::toString() and fromString() simple id round-trip
// For locator with only id, toString() should return id itself.
// ---------------------------------------------------------------------------
TEST_CASE("CORE-018: ModuleLocator toString round-trips simple id", "[core][edge]") {
    ModuleLocator loc("my-id");
    REQUIRE(loc.toString() == "my-id");

    auto parsed = ModuleLocator::fromString(loc.toString());
    REQUIRE_FALSE(parsed.isEmpty());
    REQUIRE(parsed.id() == "my-id");
    REQUIRE(parsed.package().empty());
}

// ---------------------------------------------------------------------------
// CORE-019: ModuleLocator::toString() and fromString() package/id round-trip
// For locator with package and id, toString() should return "package/id".
// ---------------------------------------------------------------------------
TEST_CASE("CORE-019: ModuleLocator toString round-trips package/id", "[core][edge]") {
    ModuleLocator loc("my-pkg", "my-id");
    REQUIRE(loc.toString() == "my-pkg/my-id");

    auto parsed = ModuleLocator::fromString(loc.toString());
    REQUIRE_FALSE(parsed.isEmpty());
    REQUIRE(parsed.package() == "my-pkg");
    REQUIRE(parsed.id() == "my-id");
}

// ---------------------------------------------------------------------------
// CORE-020: ModuleLocator equality and inequality operators
// Locators with same constructor arguments should be equal; any field differing should be unequal.
// ---------------------------------------------------------------------------
TEST_CASE("CORE-020: ModuleLocator equality operators", "[core][edge]") {
    ModuleLocator a("pkg", "id1");
    ModuleLocator b("pkg", "id1");
    ModuleLocator c("pkg", "id2");

    REQUIRE(a == b);
    REQUIRE_FALSE(a != b);
    REQUIRE(a != c);
    REQUIRE_FALSE(a == c);
}

// ---------------------------------------------------------------------------
// CORE-021: ModuleLocator::isValidLocator behavior on various inputs
// Empty string invalid; pure alphanumeric valid; composite tokens with '[' ']' '/' separators
// are processed as a whole by fromString (isValidLocator only validates single-segment token charset).
// ---------------------------------------------------------------------------
TEST_CASE("CORE-021: ModuleLocator isValidLocator behavior", "[core][edge]") {
    REQUIRE_FALSE(ModuleLocator::isValidLocator(""));
    REQUIRE(ModuleLocator::isValidLocator("valid-id"));
    REQUIRE(ModuleLocator::isValidLocator("valid_id_123"));
    // Token containing ':' is judged invalid by isValidLocator (already covered by CORE-012)
    REQUIRE_FALSE(ModuleLocator::isValidLocator("invalid:id"));
}

// ---------------------------------------------------------------------------
// CORE-022: ModuleSpec::State enum coverage
// Ensure 5 state values are distinct integers (Invalid/Initialized/Ready/Finished/Deleted),
// preventing enum values from being accidentally merged or duplicated.
// ---------------------------------------------------------------------------
TEST_CASE("CORE-022: ModuleSpec::State enum values are distinct", "[core][edge]") {
    using S = ModuleSpec::State;
    REQUIRE(S::Invalid != S::Initialized);
    REQUIRE(S::Initialized != S::Ready);
    REQUIRE(S::Ready != S::Finished);
    REQUIRE(S::Finished != S::Deleted);
    REQUIRE(S::Invalid != S::Ready);
    REQUIRE(S::Invalid != S::Finished);
    REQUIRE(S::Invalid != S::Deleted);
}

// ===========================================================================
// CORE-023 ~ CORE-032: Third round of extended edge cases (ModuleLocator version format,
// toString full path, operator comparison, isValidLocator special chars, fromString parsing).
// All cases only use APIs declared in Module.h.
// ===========================================================================

// ---------------------------------------------------------------------------
// CORE-023: ModuleLocator package+version toString format "package[version]"
// When package and version exist but no id, toString should return "package[version]".
// ---------------------------------------------------------------------------
TEST_CASE("CORE-023: ModuleLocator toString with version format", "[core][edge]") {
    stdc::VersionNumber ver(1, 2, 3);
    ModuleLocator loc("my-pkg", ver);
    REQUIRE_FALSE(loc.isEmpty());
    REQUIRE(loc.package() == "my-pkg");
    REQUIRE(loc.id().empty());
    // toString format: "my-pkg[1.2.3]"
    REQUIRE(loc.toString() == "my-pkg[1.2.3]");
}

// ---------------------------------------------------------------------------
// CORE-024: ModuleLocator full toString format "package[version]/id"
// When all three fields have values, toString should return "package[version]/id".
// ---------------------------------------------------------------------------
TEST_CASE("CORE-024: ModuleLocator toString with all fields", "[core][edge]") {
    stdc::VersionNumber ver(2, 0);
    ModuleLocator loc("pkg", ver, "mod-id");
    REQUIRE(loc.toString() == "pkg[2.0]/mod-id");
}

// ---------------------------------------------------------------------------
// CORE-025: ModuleLocator operator== returns false when versions differ
// Locators with same package/id but different version should be unequal.
// ---------------------------------------------------------------------------
TEST_CASE("CORE-025: ModuleLocator inequality with different versions",
          "[core][edge]") {
    stdc::VersionNumber v1(1, 0, 0);
    stdc::VersionNumber v2(2, 0, 0);
    ModuleLocator a("pkg", v1, "id");
    ModuleLocator b("pkg", v2, "id");
    ModuleLocator c("pkg", v1, "id");

    REQUIRE(a == c);
    REQUIRE(a != b);
    REQUIRE_FALSE(a == b);
}

// ---------------------------------------------------------------------------
// CORE-026: ModuleLocator package-only toString (no version no id)
// When only package exists, toString should return package itself.
// ---------------------------------------------------------------------------
TEST_CASE("CORE-026: ModuleLocator toString package-only", "[core][edge]") {
    ModuleLocator loc("standalone-pkg", stdc::VersionNumber{});
    // version empty, id empty → toString returns package
    REQUIRE(loc.toString() == "standalone-pkg");
    REQUIRE_FALSE(loc.isEmpty());
}

// ---------------------------------------------------------------------------
// CORE-027: ModuleLocator::isValidLocator rejects special chars
// Tokens containing '/' '\\' '[' ']' ':' ';' '\'' '"' should be judged invalid.
// ---------------------------------------------------------------------------
TEST_CASE("CORE-027: ModuleLocator isValidLocator rejects special chars",
          "[core][edge]") {
    REQUIRE_FALSE(ModuleLocator::isValidLocator("has/slash"));
    REQUIRE_FALSE(ModuleLocator::isValidLocator("has\\backslash"));
    REQUIRE_FALSE(ModuleLocator::isValidLocator("has[bracket"));
    REQUIRE_FALSE(ModuleLocator::isValidLocator("has]bracket"));
    REQUIRE_FALSE(ModuleLocator::isValidLocator("has:colon"));
    REQUIRE_FALSE(ModuleLocator::isValidLocator("has;semicolon"));
    REQUIRE_FALSE(ModuleLocator::isValidLocator("has'quote"));
    REQUIRE_FALSE(ModuleLocator::isValidLocator("has\"dquote"));
    // Valid token
    REQUIRE(ModuleLocator::isValidLocator("valid.token-123"));
    REQUIRE(ModuleLocator::isValidLocator("a"));
}

// ---------------------------------------------------------------------------
// CORE-028: ModuleLocator::fromString parses "package[version]/id" format
// fromString should correctly parse full locator containing version.
// ---------------------------------------------------------------------------
TEST_CASE("CORE-028: ModuleLocator fromString parses versioned format",
          "[core][edge]") {
    auto loc = ModuleLocator::fromString("mypkg[1.0.0]/myid");
    REQUIRE_FALSE(loc.isEmpty());
    REQUIRE(loc.package() == "mypkg");
    REQUIRE(loc.id() == "myid");
    REQUIRE_FALSE(loc.version().isEmpty());
}

// ---------------------------------------------------------------------------
// CORE-029: ModuleLocator::fromString empty string returns empty locator
// ---------------------------------------------------------------------------
TEST_CASE("CORE-029: ModuleLocator fromString empty returns empty", "[core][edge]") {
    auto loc = ModuleLocator::fromString("");
    REQUIRE(loc.isEmpty());
    REQUIRE(loc.package().empty());
    REQUIRE(loc.id().empty());
}

// ---------------------------------------------------------------------------
// CORE-030: ModuleLocator copy construction preserves all fields
// Copied locator should be completely equal to original locator.
// ---------------------------------------------------------------------------
TEST_CASE("CORE-030: ModuleLocator copy construction preserves fields",
          "[core][edge]") {
    stdc::VersionNumber ver(3, 1, 4);
    ModuleLocator original("pkg", ver, "id");
    ModuleLocator copied = original; // copy construction

    REQUIRE(copied == original);
    REQUIRE(copied.package() == "pkg");
    REQUIRE(copied.id() == "id");
    REQUIRE_FALSE(copied.version().isEmpty());
    REQUIRE(copied.toString() == original.toString());
}

// ---------------------------------------------------------------------------
// CORE-031: ModuleLocator assignment operator preserves fields
// Assigned locator should be completely equal to source locator.
// ---------------------------------------------------------------------------
TEST_CASE("CORE-031: ModuleLocator assignment preserves fields", "[core][edge]") {
    ModuleLocator source("src-pkg", "src-id");
    ModuleLocator target;
    REQUIRE(target.isEmpty());

    target = source;
    REQUIRE(target == source);
    REQUIRE(target.package() == "src-pkg");
    REQUIRE(target.id() == "src-id");
}

// ---------------------------------------------------------------------------
// CORE-032: ModuleSpec::State enum values are non-negative integers
// Ensure State enum underlying values are valid non-negative integers (Invalid should be 0).
// ---------------------------------------------------------------------------
TEST_CASE("CORE-032: ModuleSpec::State Invalid is zero", "[core][edge]") {
    using S = ModuleSpec::State;
    REQUIRE(static_cast<int>(S::Invalid) == 0);
    REQUIRE(static_cast<int>(S::Initialized) > 0);
    REQUIRE(static_cast<int>(S::Ready) > static_cast<int>(S::Initialized));
    REQUIRE(static_cast<int>(S::Finished) > static_cast<int>(S::Ready));
    REQUIRE(static_cast<int>(S::Deleted) > static_cast<int>(S::Finished));
}

// ===========================================================================
// CORE-033 ~ CORE-040: Fourth round of regression tests for recently fixed bugs.
// Covers DependencyResolver level-dedup (CORE-033), ObjectPool objectAdded
// callback (CORE-034/035), ConfigAccessor validateIntRange error message
// spacing (CORE-036), ModuleLocator special-char toString round-trip (CORE-037),
// Runtime loadPackage rollback double-free (CORE-038, P2/UB SKIP),
// DependencyResolver empty requirements (CORE-039), and ModuleLocator
// operator== with different versions (CORE-040). Uses only APIs declared in
// the included headers.
// ===========================================================================

namespace {

    // Helper: build a ModuleMetadata with default context for dependency tests.
    srt::dependency::ModuleMetadata makeMetaModule(const std::string &packageId,
                                                    const std::string &moduleId,
                                                    const std::string &version,
                                                    int level = 0) {
        srt::dependency::ModuleMetadata m;
        m.context = "default";
        m.packageId = packageId;
        m.moduleId = moduleId;
        m.version = version;
        m.level = level;
        return m;
    }

    // Helper: build a DependencyRequirement with a wildcard version range.
    srt::dependency::DependencyRequirement makeMetaReq(const std::string &packageId,
                                                       const std::string &moduleId,
                                                       int level = -1) {
        srt::dependency::DependencyRequirement r;
        r.packageId = packageId;
        r.moduleId = moduleId;
        r.level = level;
        r.versionRange = "*";
        return r;
    }

    // ObjectPool subclass that records objectAdded callback invocations.
    class TrackingPool : public ObjectPool {
    public:
        int callCount = 0;
        std::string lastId;
        NO<NamedObject> lastObj;

    protected:
        void objectAdded(std::string_view id, const NO<NamedObject> &obj) override {
            ++callCount;
            lastId = std::string(id);
            lastObj = obj;
        }
    };

} // namespace

// ---------------------------------------------------------------------------
// CORE-033: DependencyResolver resolveAllDependencies with same module at
// different levels. A module requiring the same package::module at two
// different levels must NOT skip the second requirement (regression: the
// alreadyResolved check must compare level, not just packageId/moduleId).
// ---------------------------------------------------------------------------
TEST_CASE("CORE-033: resolveAllDependencies resolves same module at different levels",
          "[core][edge]") {
    srt::dependency::DependencyResolver resolver;

    std::vector<srt::dependency::ModuleMetadata> modules;
    auto a = makeMetaModule("pkgA", "A", "1.0.0", 0);
    a.requirements.push_back(makeMetaReq("pkgB", "B", 1));
    a.requirements.push_back(makeMetaReq("pkgB", "B", 2));
    modules.push_back(a);

    modules.push_back(makeMetaModule("pkgB", "B", "1.0.0", 1));
    modules.push_back(makeMetaModule("pkgB", "B", "1.0.0", 2));

    bool ok = resolver.resolveAllDependencies(modules);
    REQUIRE(ok);
    REQUIRE(resolver.getErrors().empty());

    const auto &resolved = resolver.getResolvedModules();
    const srt::dependency::ModuleMetadata *aResolved = nullptr;
    for (const auto &m : resolved) {
        if (m.moduleId == "A")
            aResolved = &m;
    }
    REQUIRE(aResolved != nullptr);

    // Both requirements (level 1 and level 2) must be resolved, not just one.
    REQUIRE(aResolved->resolvedDependencies.size() == 2);
    bool hasLevel1 = false;
    bool hasLevel2 = false;
    for (const auto &dep : aResolved->resolvedDependencies) {
        if (dep.level == 1)
            hasLevel1 = true;
        if (dep.level == 2)
            hasLevel2 = true;
    }
    REQUIRE(hasLevel1);
    REQUIRE(hasLevel2);
}

// ---------------------------------------------------------------------------
// CORE-034: ObjectPool addObject invokes objectAdded callback with the
// correct id and obj.
// ---------------------------------------------------------------------------
TEST_CASE("CORE-034: ObjectPool addObject invokes objectAdded callback",
          "[core][edge]") {
    TrackingPool pool;
    REQUIRE(pool.callCount == 0);

    auto obj = NO<NamedObject>::create("test-obj");
    pool.addObject("category", obj);

    REQUIRE(pool.callCount == 1);
    REQUIRE(pool.lastId == "category");
    REQUIRE(pool.lastObj == obj);
    REQUIRE(pool.getObjects("category").size() == 1);
}

// ---------------------------------------------------------------------------
// CORE-035: ObjectPool addObject with null NO is a no-op (no crash, no
// callback). Regression: null obj must early-return before invoking
// objectAdded.
// ---------------------------------------------------------------------------
TEST_CASE("CORE-035: ObjectPool addObject with null NO is a no-op",
          "[core][edge]") {
    TrackingPool pool;
    NO<NamedObject> nullObj;

    REQUIRE_NOTHROW(pool.addObject("category", nullObj));
    REQUIRE_NOTHROW(pool.addObject(nullObj));

    // Callback must not fire for null input.
    REQUIRE(pool.callCount == 0);
    REQUIRE(pool.getObjects("category").empty());
    REQUIRE(pool.allObjects().empty());
}

// ---------------------------------------------------------------------------
// CORE-036: ConfigAccessor validateIntRange error message has correct spacing.
// Regression: the format string must include a space before "is" so the
// message reads "Value X is out of range" (not "Value Xis out of range").
// validateIntRange is a static method, so no ConfigAccessor instance is needed.
// ---------------------------------------------------------------------------
TEST_CASE("CORE-036: ConfigAccessor validateIntRange error message spacing",
          "[core][edge]") {
    auto result = ConfigAccessor::validateIntRange(50, 0, 10, "count");
    REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error().type() == Error::InvalidArgument);

    const auto &msg = result.error().message();
    // The fix ensures a space separates the value/key and "is out of range".
    REQUIRE(msg.find(" is out of range") != std::string::npos);
    // Sanity: the key and bounds are present in the message.
    REQUIRE(msg.find("count") != std::string::npos);
    REQUIRE(msg.find("50") != std::string::npos);
}

// ---------------------------------------------------------------------------
// CORE-037: ModuleLocator with special characters (dots, dashes, digits) in
// package and id names round-trips through toString/fromString.
// ---------------------------------------------------------------------------
TEST_CASE("CORE-037: ModuleLocator special chars toString round-trip",
          "[core][edge]") {
    SECTION("package/id with dots, dashes, digits") {
        ModuleLocator loc("pkg.with.dots-123", "mod-id.with.dots-456");
        REQUIRE_FALSE(loc.isEmpty());

        const auto str = loc.toString();
        REQUIRE(str == "pkg.with.dots-123/mod-id.with.dots-456");

        auto parsed = ModuleLocator::fromString(str);
        REQUIRE_FALSE(parsed.isEmpty());
        REQUIRE(parsed == loc);
        REQUIRE(parsed.package() == "pkg.with.dots-123");
        REQUIRE(parsed.id() == "mod-id.with.dots-456");
    }

    SECTION("package[version]/id with dots and dashes") {
        stdc::VersionNumber ver(1, 2, 3);
        ModuleLocator loc("pkg.dots-1", ver, "id-dash.2");
        const auto str = loc.toString();
        REQUIRE(str == "pkg.dots-1[1.2.3]/id-dash.2");

        auto parsed = ModuleLocator::fromString(str);
        REQUIRE(parsed == loc);
        REQUIRE(parsed.package() == "pkg.dots-1");
        REQUIRE(parsed.id() == "id-dash.2");
        REQUIRE_FALSE(parsed.version().isEmpty());
    }
}

// ---------------------------------------------------------------------------
// CORE-039: DependencyResolver with empty requirements list. A module with no
// requirements should be immediately resolved (resolvedDependencies.size()
// == requirements.size() == 0).
// ---------------------------------------------------------------------------
TEST_CASE("CORE-039: DependencyResolver empty requirements resolves immediately",
          "[core][edge]") {
    srt::dependency::DependencyResolver resolver;

    std::vector<srt::dependency::ModuleMetadata> modules;
    modules.push_back(makeMetaModule("pkg", "M", "1.0.0", 0));

    bool ok = resolver.resolveAllDependencies(modules);
    REQUIRE(ok);
    REQUIRE(resolver.getErrors().empty());

    const auto &resolved = resolver.getResolvedModules();
    REQUIRE(resolved.size() == 1);
    REQUIRE(resolved[0].moduleId == "M");
    REQUIRE(resolved[0].resolvedDependencies.empty());
}

// ---------------------------------------------------------------------------
// CORE-040: ModuleLocator operator== with different versions. Two locators
// with the same package/id but different versions must not be equal.
// Complements CORE-025 (major version diff) by covering a patch version diff.
// ---------------------------------------------------------------------------
TEST_CASE("CORE-040: ModuleLocator operator== with different versions",
          "[core][edge]") {
    stdc::VersionNumber v1(1, 5, 0);
    stdc::VersionNumber v2(1, 5, 1);
    ModuleLocator a("pkg", v1, "id");
    ModuleLocator b("pkg", v2, "id");
    ModuleLocator c("pkg", v1, "id");

    // Reflexive & symmetric equality for identical locators.
    REQUIRE(a == c);
    REQUIRE_FALSE(a != c);

    // Same package/id but different version (patch) must be unequal.
    REQUIRE(a != b);
    REQUIRE_FALSE(a == b);
}
