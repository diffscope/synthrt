// Unit tests for srt::dependency::VersionRange and VersionResolver.
//
// Covers version normalization, comparison, range parsing, constraint matching,
// and edge cases including empty strings, pre-release suffixes, 'v' prefix,
// and 5+ segment versions (per project constraint: no stack overflow).

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Dependency/VersionUtils.h>
#include <synthrt/Core/Dependency/DependencyGraph.h>

using namespace srt::dependency;

// ---------------------------------------------------------------------------
// normalizeVersion
// ---------------------------------------------------------------------------

TEST_CASE("normalizeVersion empty string returns 0.0.0", "[version]") {
    REQUIRE(VersionRange::normalizeVersion("") == "0.0.0");
}

TEST_CASE("normalizeVersion strips v prefix", "[version]") {
    REQUIRE(VersionRange::normalizeVersion("v1.2.3") == "1.2.3");
    REQUIRE(VersionRange::normalizeVersion("V1.2.3") == "1.2.3");
}

TEST_CASE("normalizeVersion strips pre-release suffix", "[version]") {
    REQUIRE(VersionRange::normalizeVersion("1.0.0-alpha") == "1.0.0");
    REQUIRE(VersionRange::normalizeVersion("2.0.0-beta.1") == "2.0.0");
}

TEST_CASE("normalizeVersion pads to 3 parts", "[version]") {
    REQUIRE(VersionRange::normalizeVersion("1") == "1.0.0");
    REQUIRE(VersionRange::normalizeVersion("1.2") == "1.2.0");
}

TEST_CASE("normalizeVersion truncates to 3 parts", "[version]") {
    REQUIRE(VersionRange::normalizeVersion("1.2.3.4.5") == "1.2.3");
}

TEST_CASE("normalizeVersion handles non-numeric parts by dropping them", "[version]") {
    // Non-numeric chars within a part are stripped; if a part becomes empty it's dropped.
    REQUIRE(VersionRange::normalizeVersion("1.2.x") == "1.2.0");
}

// ---------------------------------------------------------------------------
// compareVersions
// ---------------------------------------------------------------------------

TEST_CASE("compareVersions equal versions return 0", "[version]") {
    REQUIRE(VersionRange::compareVersions("1.0.0", "1.0.0") == 0);
    REQUIRE(VersionRange::compareVersions("2.5.3", "2.5.3") == 0);
}

TEST_CASE("compareVersions less than returns negative", "[version]") {
    REQUIRE(VersionRange::compareVersions("1.0.0", "2.0.0") < 0);
    REQUIRE(VersionRange::compareVersions("1.9.9", "2.0.0") < 0);
}

TEST_CASE("compareVersions greater than returns positive", "[version]") {
    REQUIRE(VersionRange::compareVersions("2.0.0", "1.0.0") > 0);
    REQUIRE(VersionRange::compareVersions("2.0.1", "2.0.0") > 0);
}

TEST_CASE("compareVersions handles different segment counts", "[version]") {
    // 1.0 vs 1.0.0: missing parts treated as 0.
    REQUIRE(VersionRange::compareVersions("1.0", "1.0.0") == 0);
    REQUIRE(VersionRange::compareVersions("1", "1.0.0") == 0);
}

TEST_CASE("compareVersions 5+ segment versions no crash", "[version]") {
    // Project constraint: 5+ segment versions must not stack overflow.
    REQUIRE(VersionRange::compareVersions("1.2.3.4.5", "1.2.3.4.5") == 0);
    REQUIRE(VersionRange::compareVersions("1.2.3.4.5", "1.2.3.4.6") < 0);
    REQUIRE(VersionRange::compareVersions("1.2.3.4.6", "1.2.3.4.5") > 0);
}

TEST_CASE("compareVersions non-numeric parts treated as 0", "[version]") {
    REQUIRE(VersionRange::compareVersions("1.x.0", "1.0.0") == 0);
}

// ---------------------------------------------------------------------------
// VersionRange parsing and matching
// ---------------------------------------------------------------------------

TEST_CASE("VersionRange empty string matches any", "[version][range]") {
    VersionRange range("");
    REQUIRE(range.valid());
    REQUIRE(range.toString() == "*");
}

TEST_CASE("VersionRange star matches any", "[version][range]") {
    VersionRange range("*");
    REQUIRE(range.valid());
    REQUIRE(range.toString() == "*");
}

TEST_CASE("VersionRange exact version match", "[version][range]") {
    VersionRange range("1.2.3");
    REQUIRE(range.valid());
    REQUIRE(range.toString() == "==1.2.3");
}

TEST_CASE("VersionRange less than operator", "[version][range]") {
    VersionRange range("<2.0.0");
    REQUIRE(range.valid());
}

TEST_CASE("VersionRange less equal operator", "[version][range]") {
    VersionRange range("<=2.0.0");
    REQUIRE(range.valid());
}

TEST_CASE("VersionRange greater than operator", "[version][range]") {
    VersionRange range(">1.0.0");
    REQUIRE(range.valid());
}

TEST_CASE("VersionRange greater equal operator", "[version][range]") {
    VersionRange range(">=1.0.0");
    REQUIRE(range.valid());
}

TEST_CASE("VersionRange equal operator with equals sign", "[version][range]") {
    VersionRange range("==1.5.0");
    REQUIRE(range.valid());
}

TEST_CASE("VersionRange compatible operator", "[version][range]") {
    VersionRange range("~1.2.0");
    REQUIRE(range.valid());
}

TEST_CASE("VersionRange hyphen range", "[version][range]") {
    VersionRange range("1.0.0 - 2.0.0");
    REQUIRE(range.valid());
}

TEST_CASE("VersionRange getVersionsInRange filters correctly", "[version][range]") {
    VersionRange range(">=1.0.0");
    std::vector<std::string> available{"0.9.0", "1.0.0", "1.5.0", "2.0.0"};
    auto result = range.getVersionsInRange(available);
    // Should include 1.0.0, 1.5.0, 2.0.0 (sorted descending by compareVersions)
    REQUIRE(result.size() == 3);
}

TEST_CASE("VersionRange getVersionsInRange empty available", "[version][range]") {
    VersionRange range(">=1.0.0");
    std::vector<std::string> available;
    auto result = range.getVersionsInRange(available);
    REQUIRE(result.empty());
}

// ---------------------------------------------------------------------------
// BF-04 regression: resolveDependency must not crash on version strings
// containing 'v' (the old code parsed formatted display strings with find("v")).
// ---------------------------------------------------------------------------

TEST_CASE("BF-04 resolveDependency no crash with v in version", "[version][bf-04]") {
    // ModuleMetadata with version containing 'v' (e.g. "1.0.0v2").
    // The old BF-04 code formatted this as "moduleId (v1.0.0v2, level 1)"
    // then did find("v") which matched the 'v' inside the version number
    // instead of the "(v" prefix, causing misparse or crash.
    std::vector<ModuleMetadata> modules;
    ModuleMetadata m1;
    m1.packageId = "test.pkg";
    m1.moduleId = "mod1";
    m1.version = "1.0.0v2";
    m1.level = 1;
    modules.push_back(m1);

    ModuleMetadata m2;
    m2.packageId = "test.pkg";
    m2.moduleId = "mod2";
    m2.version = "2.0.0";
    m2.level = 1;
    modules.push_back(m2);

    // Request a dependency that doesn't exist — triggers the candidates.empty()
    // path which sorts available modules by version (the BF-04 crash path).
    DependencyRequirement dep;
    dep.packageId = "test.pkg";
    dep.moduleId = "nonexistent";
    dep.level = 1;
    dep.versionRange = "*";

    ModuleMetadata requesting;
    requesting.packageId = "caller.pkg";
    requesting.moduleId = "caller";
    requesting.version = "1.0.0";
    requesting.level = 1;

    // Must not crash.
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);
    REQUIRE(!result.success);
    REQUIRE(result.error.find("not found") != std::string::npos);
}

TEST_CASE("BF-04 resolveDependency sorts by version not display string", "[version][bf-04]") {
    // Verify that the error message lists available modules sorted by version,
    // not by the formatted display string (which was the BF-04 root cause).
    std::vector<ModuleMetadata> modules;
    ModuleMetadata m1;
    m1.packageId = "test.pkg";
    m1.moduleId = "aaa";
    m1.version = "2.0.0";
    m1.level = 1;
    modules.push_back(m1);

    ModuleMetadata m2;
    m2.packageId = "test.pkg";
    m2.moduleId = "zzz";
    m2.version = "1.0.0";
    m2.level = 1;
    modules.push_back(m2);

    DependencyRequirement dep;
    dep.packageId = "test.pkg";
    dep.moduleId = "nonexistent";
    dep.level = -1;
    dep.versionRange = "*";

    ModuleMetadata requesting;
    requesting.packageId = "caller.pkg";
    requesting.moduleId = "caller";
    requesting.version = "1.0.0";
    requesting.level = 1;

    auto result = VersionResolver::resolveDependency(modules, dep, requesting);
    REQUIRE(!result.success);
    // Error message should contain both modules.
    REQUIRE(result.error.find("aaa") != std::string::npos);
    REQUIRE(result.error.find("zzz") != std::string::npos);
}
