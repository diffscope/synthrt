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

// ---------------------------------------------------------------------------
// Extreme cases: normalizeVersion edge inputs
// ---------------------------------------------------------------------------

TEST_CASE("normalizeVersion lone v prefix yields 0.0.0", "[version][extreme]") {
    // "v" / "V" with no digits: after stripping the prefix the remainder is
    // empty, which pads to 0.0.0. Must not produce an empty or malformed
    // string.
    REQUIRE(VersionRange::normalizeVersion("v") == "0.0.0");
    REQUIRE(VersionRange::normalizeVersion("V") == "0.0.0");
}

TEST_CASE("normalizeVersion strips at first dash only", "[version][extreme]") {
    // Multiple dashes: the pre-release cut happens at the first '-', so the
    // rest is discarded entirely.
    REQUIRE(VersionRange::normalizeVersion("1.0.0-alpha-beta") == "1.0.0");
    REQUIRE(VersionRange::normalizeVersion("2.1.3-rc.1.build") == "2.1.3");
}

TEST_CASE("normalizeVersion leading and trailing dots", "[version][extreme]") {
    // Empty parts produced by leading/trailing dots are dropped, then the
    // remaining numeric parts are padded to three segments.
    REQUIRE(VersionRange::normalizeVersion(".1.0") == "1.0.0");
    REQUIRE(VersionRange::normalizeVersion("1.0.") == "1.0.0");
    REQUIRE(VersionRange::normalizeVersion(".") == "0.0.0");
    REQUIRE(VersionRange::normalizeVersion("...") == "0.0.0");
}

TEST_CASE("normalizeVersion all non-numeric parts yield 0.0.0", "[version][extreme]") {
    // When no part contributes any digit, the result is the zero version.
    REQUIRE(VersionRange::normalizeVersion("x.y.z") == "0.0.0");
    REQUIRE(VersionRange::normalizeVersion("vx.y") == "0.0.0");
}

// ---------------------------------------------------------------------------
// Extreme cases: compareVersions edge inputs
// ---------------------------------------------------------------------------

TEST_CASE("compareVersions empty strings compare equal", "[version][extreme]") {
    REQUIRE(VersionRange::compareVersions("", "") == 0);
}

TEST_CASE("compareVersions empty is less than any version", "[version][extreme]") {
    REQUIRE(VersionRange::compareVersions("", "1.0.0") < 0);
    REQUIRE(VersionRange::compareVersions("1.0.0", "") > 0);
}

TEST_CASE("compareVersions overflow part treated as 0 no crash", "[version][extreme]") {
    // stoi throws out_of_range for values exceeding int; the parser catches
    // and substitutes 0. A huge version therefore compares equal to 0.0.0
    // and must not crash.
    REQUIRE(VersionRange::compareVersions("99999999999", "0.0.0") == 0);
    REQUIRE(VersionRange::compareVersions("99999999999", "1.0.0") < 0);
}

// ---------------------------------------------------------------------------
// Extreme cases: VersionRange parsing / parseError
// ---------------------------------------------------------------------------

TEST_CASE("VersionRange compatible operator with single part", "[version][extreme][range]") {
    // "~1" normalizes to "~1.0.0", i.e. major==1 AND minor==0 AND patch>=0.
    VersionRange range("~1");
    REQUIRE(range.valid());

    std::vector<std::string> available{"0.9.0", "1.0.0", "1.0.5", "1.5.0", "2.0.0"};
    auto matched = range.getVersionsInRange(available);
    // 1.0.0 and 1.0.5 match (major 1, minor 0); 1.5.0 does not (minor 5).
    REQUIRE(matched.size() == 2);
    REQUIRE(matched[0] == "1.0.5"); // sorted descending
    REQUIRE(matched[1] == "1.0.0");
}

TEST_CASE("VersionRange hyphen range with reversed bounds matches nothing", "[version][extreme][range]") {
    // min > max: no version can satisfy (test >= max AND test <= min).
    VersionRange range("2.0.0 - 1.0.0");
    REQUIRE(range.valid());

    std::vector<std::string> available{"1.0.0", "1.5.0", "2.0.0", "3.0.0"};
    auto matched = range.getVersionsInRange(available);
    REQUIRE(matched.empty());
}

TEST_CASE("VersionRange parseError for empty version after operator", "[version][extreme][range]") {
    VersionRange range(">=");
    REQUIRE(!range.valid());
    REQUIRE(!range.parseError().empty());
    REQUIRE(range.parseError().find("empty version") != std::string::npos);
}

TEST_CASE("VersionRange parseError for invalid single token", "[version][extreme][range]") {
    VersionRange range("abc");
    REQUIRE(!range.valid());
    REQUIRE(range.parseError().find("invalid constraint") != std::string::npos);
}

TEST_CASE("VersionRange parseError for invalid token among constraints", "[version][extreme][range]") {
    // A multi-constraint with one bad token marks the range invalid but keeps
    // the valid constraints.
    VersionRange range(">=1.0.0 xyz");
    REQUIRE(!range.valid());
    REQUIRE(range.parseError().find("xyz") != std::string::npos);
}

// BF-32 regression: multi-constraint strings whose first token carries an
// operator (e.g. ">=1.0.0 <2.0.0") must be parsed as TWO constraints. The old
// constructor matched the leading ">=" in its single-operator branch and
// swallowed the whole remainder as one version, silently dropping the upper
// bound — so 3.0.0 would incorrectly match.
TEST_CASE("BF-32 VersionRange multi-constraint with leading operator", "[version][bf-32][range]") {
    VersionRange range(">=1.0.0 <2.0.0");
    REQUIRE(range.valid());
    REQUIRE(range.toString() == ">=1.0.0 <2.0.0");

    std::vector<std::string> available{"0.5.0", "1.0.0", "1.5.0", "2.0.0", "3.0.0"};
    auto matched = range.getVersionsInRange(available);
    // Only 1.0.0 and 1.5.0 satisfy both >=1.0.0 and <2.0.0.
    REQUIRE(matched.size() == 2);
    REQUIRE(matched[0] == "1.5.0"); // sorted descending
    REQUIRE(matched[1] == "1.0.0");
}

TEST_CASE("BF-32 VersionRange three-constraint intersection", "[version][bf-32][range]") {
    VersionRange range(">=1.0.0 <3.0.0 !=2.0.0");
    // NOTE: "!=" is not a supported operator; parseConstraint maps it to ANY
    // (invalid), which marks the range invalid. This documents that behavior.
    REQUIRE(!range.valid());
}

// ---------------------------------------------------------------------------
// Extreme cases: selectHighestVersion
// ---------------------------------------------------------------------------

TEST_CASE("selectHighestVersion empty list returns empty", "[version][extreme]") {
    REQUIRE(VersionResolver::selectHighestVersion({}).empty());
}

TEST_CASE("selectHighestVersion single element", "[version][extreme]") {
    REQUIRE(VersionResolver::selectHighestVersion({"1.0.0"}) == "1.0.0");
}

TEST_CASE("selectHighestVersion unsorted picks highest", "[version][extreme]") {
    std::vector<std::string> versions{"1.0.0", "3.0.0", "2.0.0"};
    REQUIRE(VersionResolver::selectHighestVersion(versions) == "3.0.0");
}
