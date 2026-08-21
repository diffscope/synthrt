// lib/Core/Dependency edge condition test cases (DEP-001 ~ DEP-010)
//
// Cover DependencyGraph / VersionUtils / LevelCompatibilityChecker under cycle detection,
// malformed version numbers, Level compatibility and other edge scenarios. Corresponds to
// namespace srt::dependency (see docs/refactoring-vnext/03-conventions.md §2.1).
//
// === API difference notes from test matrix ===
// The actual behavior of the following cases differs from the matrix description
// (adjusted per actual API):
//   - DEP-001: Matrix expected "Error(DependencyCycle)". DependencyGraph at graph level
//     skips self-dependency (depNode == node does not add edge), buildGraph returns true
//     and findCycles() is empty (see lib/Core/Dependency/DependencyGraph.cpp).
//     Real self-dependency detection is done in DependencyResolver::resolveAllDependencies:
//     it removes self-dependent modules and writes errors to getErrors(). This case verifies both.
//   - DEP-002: Matrix expected "Error(DependencyCycle)". DependencyGraph::buildGraph
//     returns true for cycles (does not fail), cycles are exposed via findCycles().
//     This case asserts findCycles() is non-empty.
//   - DEP-004/005/006/010: stdc::VersionNumber has no string constructor,
//     must use VersionNumber::fromString(...).value_or(stdc::VersionNumber()). VersionNumber("...")
//     syntax in matrix is uniformly mapped to fromString. fromString accepts only
//     1..4 dot-separated pure-digit components and returns nullopt otherwise
//     (5+ segments, empty string, non-digit chars, leading dot), so malformed
//     input must be handled with has_value()/value_or() — never .value() directly.

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Dependency/DependencyGraph.h>
#include <synthrt/Core/Dependency/DependencyResolver.h>
#include <synthrt/Core/Dependency/LevelCompatibilityChecker.h>
#include <synthrt/Core/Dependency/VersionUtils.h>

using srt::dependency::DependencyGraph;
using srt::dependency::DependencyRequirement;
using srt::dependency::DependencyResolver;
using srt::dependency::LevelCompatibilityChecker;
using srt::dependency::ModuleMetadata;
using srt::dependency::ResolvedDependency;
using stdc::VersionNumber;

namespace {

    ModuleMetadata makeModule(const std::string &packageId, const std::string &moduleId,
                              const std::string &version, int level = 0,
                              const std::string &context = "default") {
        ModuleMetadata m;
        m.context = context;
        m.packageId = packageId;
        m.moduleId = moduleId;
        m.version = version;
        m.level = level;
        return m;
    }

    ResolvedDependency makeDep(const std::string &packageId, const std::string &moduleId,
                               const std::string &version, int level = 0) {
        ResolvedDependency d;
        d.packageId = packageId;
        d.moduleId = moduleId;
        d.version = version;
        d.level = level;
        return d;
    }

    DependencyRequirement makeReq(const std::string &packageId, const std::string &moduleId,
                                  int level = -1) {
        DependencyRequirement r;
        r.packageId = packageId;
        r.moduleId = moduleId;
        r.level = level;
        r.versionRange = "*";
        return r;
    }

} // namespace

// ---------------------------------------------------------------------------
// DEP-001: DependencyGraph self-dependency cycle detection
// API difference: graph level skips self-dependency (no cycle); DependencyResolver removes self-dependent module and reports error
// ---------------------------------------------------------------------------
TEST_CASE("DEP-001: self-dependency detection", "[dep][edge]") {
    SECTION("DependencyGraph skips self-edge at graph level") {
        DependencyGraph g;
        auto a = makeModule("pkg", "A", "1.0.0");
        a.resolvedDependencies.push_back(makeDep("pkg", "A", "1.0.0"));
        g.addModule(a);

        REQUIRE(g.buildGraph());
        // Self-dependency is skipped internally by buildGraph, does not constitute a cycle
        REQUIRE(g.findCycles().empty());
    }

    SECTION("DependencyResolver detects and removes self-dependency") {
        DependencyResolver resolver;
        std::vector<ModuleMetadata> modules;
        auto a = makeModule("pkg", "A", "1.0.0", 1);
        a.requirements.push_back(makeReq("pkg", "A", 1));
        modules.push_back(a);

        bool ok = resolver.resolveAllDependencies(modules);
        REQUIRE_FALSE(ok);
        REQUIRE_FALSE(resolver.getErrors().empty());
        // Self-dependent module is removed
        REQUIRE(resolver.getResolvedModules().empty());
    }
}

// ---------------------------------------------------------------------------
// DEP-002: DependencyGraph multi-node cycle detection
// API difference: buildGraph returns true for cycles, cycles are exposed via findCycles()
// ---------------------------------------------------------------------------
TEST_CASE("DEP-002: multi-node cycle detection", "[dep][edge]") {
    DependencyGraph g;
    auto a = makeModule("pkg", "A", "1.0.0");
    auto b = makeModule("pkg", "B", "1.0.0");
    auto c = makeModule("pkg", "C", "1.0.0");
    a.resolvedDependencies.push_back(makeDep("pkg", "B", "1.0.0"));
    b.resolvedDependencies.push_back(makeDep("pkg", "C", "1.0.0"));
    c.resolvedDependencies.push_back(makeDep("pkg", "A", "1.0.0"));
    g.addModule(a);
    g.addModule(b);
    g.addModule(c);

    REQUIRE(g.buildGraph());
    auto cycles = g.findCycles();
    REQUIRE_FALSE(cycles.empty());
    REQUIRE(cycles[0].size() == 3);
}

// ---------------------------------------------------------------------------
// DEP-003: DependencyGraph empty graph
// ---------------------------------------------------------------------------
TEST_CASE("DEP-003: empty graph topological sort", "[dep][edge]") {
    DependencyGraph g;
    REQUIRE(g.buildGraph());
    REQUIRE(g.getAllModules().empty());
    REQUIRE(g.findCycles().empty());
    REQUIRE(g.getPackageInitializationOrder().empty());
}

// ---------------------------------------------------------------------------
// DEP-004/005/006: VersionNumber::fromString edge cases
// Merged: all three verify fromString parsing behavior on malformed input
// and share the VersionNumber construction. SECTION form preserves per-case
// traceability while removing the duplicate setup. Adds a leading-dot
// boundary case.
// ---------------------------------------------------------------------------
TEST_CASE("DEP-004/005/006: VersionNumber::fromString edge cases",
          "[dep][edge]") {
    SECTION("DEP-004: 6-segment version is rejected") {
        // stdcorelib accepts only 1..4 dot-separated pure-digit components and
        // returns nullopt for 5+ segments (the old trim-to-4 behavior was
        // removed; see stdcorelib/support/versionnumber.h fromString() docs).
        // Callers that need 4-segment trimming do it explicitly via ds::bank::
        // parseBankVersion() before fromString (BankVersion.h).
        auto v = VersionNumber::fromString("1.2.3.4.5.6");
        REQUIRE_FALSE(v.has_value());
    }

    SECTION("DEP-005: empty string is rejected (nullopt, not empty version)") {
        // A missing parse (nullopt) is distinct from a default-constructed
        // empty version; callers must use value_or(stdc::VersionNumber()).
        auto v = VersionNumber::fromString("");
        REQUIRE_FALSE(v.has_value());

        VersionNumber defaultV;
        REQUIRE(defaultV.isEmpty());
    }

    SECTION("DEP-006: non-numeric chars are rejected") {
        // Every component must be pure digits; "2abc" does not spell a version.
        auto v = VersionNumber::fromString("1.2abc.3");
        REQUIRE_FALSE(v.has_value());
    }

    SECTION("DEP-006b: leading dot is rejected") {
        // A leading dot yields an empty first component, which is not digits.
        auto v = VersionNumber::fromString(".1.2");
        REQUIRE_FALSE(v.has_value());
    }
}

// ---------------------------------------------------------------------------
// DEP-007/008: LevelCompatibilityChecker compatibility matrix
// Merged: both share the LevelConfig construction and verify the
// compatible/incompatible contract. SECTION form preserves per-case
// traceability while removing the duplicate setup. Adds a same-Level
// boundary case at the minimum supported Level.
// ---------------------------------------------------------------------------
TEST_CASE("DEP-007/008: LevelCompatibilityChecker compatibility",
          "[dep][edge]") {
    LevelCompatibilityChecker::LevelConfig cfg(2, 2, 2);

    SECTION("DEP-007: same Level (2 vs 2) is compatible") {
        auto r = LevelCompatibilityChecker::checkCorePlugin(2, cfg);
        REQUIRE(r.isCompatible);
        REQUIRE(r.pluginLevel == 2);
    }

    SECTION("DEP-008: cross Level (1 vs min 2) is incompatible") {
        // System current Level=2 (minimum=2), plugin Level=1, incompatible.
        auto r = LevelCompatibilityChecker::checkCorePlugin(1, cfg);
        REQUIRE_FALSE(r.isCompatible);
        REQUIRE(r.pluginLevel == 1);
        REQUIRE_FALSE(r.suggestion.empty());
    }

    SECTION("DEP-008b: plugin Level == current == minimum is compatible") {
        // Boundary: plugin Level exactly at the minimum supported Level.
        auto r = LevelCompatibilityChecker::checkCorePlugin(2, cfg);
        REQUIRE(r.isCompatible);
    }
}

// ---------------------------------------------------------------------------
// DEP-009: DependencyGraph diamond dependency
// A→B, A→C, B→D, C→D: topological sort succeeds, D is ordered after B/C
// ---------------------------------------------------------------------------
TEST_CASE("DEP-009: diamond dependency topological order", "[dep][edge]") {
    DependencyGraph g;
    auto a = makeModule("pkg", "A", "1.0.0");
    auto b = makeModule("pkg", "B", "1.0.0");
    auto c = makeModule("pkg", "C", "1.0.0");
    auto d = makeModule("pkg", "D", "1.0.0");

    a.resolvedDependencies.push_back(makeDep("pkg", "B", "1.0.0"));
    a.resolvedDependencies.push_back(makeDep("pkg", "C", "1.0.0"));
    b.resolvedDependencies.push_back(makeDep("pkg", "D", "1.0.0"));
    c.resolvedDependencies.push_back(makeDep("pkg", "D", "1.0.0"));

    g.addModule(a);
    g.addModule(b);
    g.addModule(c);
    g.addModule(d);

    REQUIRE(g.buildGraph());
    REQUIRE(g.findCycles().empty());

    auto order = g.getPackageInitializationOrder();
    REQUIRE(order.size() == 1);
    auto &initOrder = order[0].initializationOrder;
    REQUIRE(initOrder.size() == 4);

    auto findIdx = [&](const std::string &id) {
        for (size_t i = 0; i < initOrder.size(); ++i)
            if (initOrder[i].moduleId == id) return static_cast<int>(i);
        return -1;
    };
    int idxD = findIdx("D");
    int idxB = findIdx("B");
    int idxC = findIdx("C");
    int idxA = findIdx("A");
    REQUIRE(idxD >= 0);
    REQUIRE(idxB >= 0);
    REQUIRE(idxC >= 0);
    REQUIRE(idxA >= 0);
    // D must precede B/C; B/C must precede A
    REQUIRE(idxD < idxB);
    REQUIRE(idxD < idxC);
    REQUIRE(idxB < idxA);
    REQUIRE(idxC < idxA);
}

// ---------------------------------------------------------------------------
// DEP-010: VersionUtils compares null/empty version
// API difference: uses VersionNumber() default construction compared with fromString("1.0")
// ---------------------------------------------------------------------------
TEST_CASE("DEP-010: compare null/empty version against 1.0", "[dep][edge]") {
    VersionNumber empty;
    auto v10 = VersionNumber::fromString("1.0").value();

    REQUIRE(empty.isEmpty());
    REQUIRE_FALSE(v10.isEmpty());

    REQUIRE(empty < v10);
    REQUIRE_FALSE(empty > v10);
    REQUIRE(empty != v10);
    REQUIRE_FALSE(empty == v10);
}
