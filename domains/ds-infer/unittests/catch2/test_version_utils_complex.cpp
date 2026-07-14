// Complex scenario tests for VersionRange, VersionResolver, and DependencyGraph.
//
// Covers real-world package management complexity:
//   - Package name conflicts (same packageId, different versions)
//   - Version range conflicts (empty intersection, overlapping ranges)
//   - Dependency cycles (A→B→A, A→B→C→A, self-dependency)
//   - Missing dependencies (required module not installed)
//   - Complex package names (dots, hyphens, mixed case, unicode-like)
//   - Multi-level dependency resolution (level filtering)
//   - DependencyGraph build + cycle detection + topological order
//   - Diamond dependency (A→B+C, B→D, C→D)

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include <synthrt/Core/Dependency/VersionUtils.h>
#include <synthrt/Core/Dependency/DependencyGraph.h>

using namespace srt::dependency;

namespace {
    ModuleMetadata makeModule(const std::string &packageId, const std::string &moduleId,
                               const std::string &version, int level) {
        ModuleMetadata m;
        m.packageId = packageId;
        m.moduleId = moduleId;
        m.version = version;
        m.level = level;
        return m;
    }

    ResolvedDependency makeResolved(const std::string &packageId, const std::string &moduleId,
                                     const std::string &version, int level) {
        ResolvedDependency d;
        d.packageId = packageId;
        d.moduleId = moduleId;
        d.version = version;
        d.level = level;
        return d;
    }

    DependencyRequirement makeRequirement(const std::string &packageId, const std::string &moduleId,
                                           int level, const std::string &versionRange = "*") {
        DependencyRequirement d;
        d.packageId = packageId;
        d.moduleId = moduleId;
        d.level = level;
        d.versionRange = versionRange;
        return d;
    }
}

// ===========================================================================
// Complex package names
// ===========================================================================

TEST_CASE("resolveDependency complex package name with dots", "[version][complex][pkgname]") {
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("org.diffinger.singer.cn", "vocal", "1.0.0", 1));
    modules.push_back(makeModule("org.diffinger.singer.cn", "vocal", "2.0.0", 1));

    auto dep = makeRequirement("org.diffinger.singer.cn", "vocal", 1, ">=1.5.0");
    auto requesting = makeModule("caller.pkg", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(result.success);
    REQUIRE(result.resolvedVersion == "2.0.0");
}

TEST_CASE("resolveDependency package name with hyphens", "[version][complex][pkgname]") {
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("diffsinger-opencpop", "acoustic", "1.0.0", 1));

    auto dep = makeRequirement("diffsinger-opencpop", "acoustic", 1, "*");
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(result.success);
    REQUIRE(result.resolvedVersion == "1.0.0");
}

TEST_CASE("resolveDependency package name with underscores", "[version][complex][pkgname]") {
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("voice_bank_zh", "singer", "3.1.0", 1));

    auto dep = makeRequirement("voice_bank_zh", "singer", 1);
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(result.success);
    REQUIRE(result.resolvedVersion == "3.1.0");
}

TEST_CASE("resolveDependency package name case sensitivity", "[version][complex][pkgname]") {
    // Package names are case-sensitive: "Pkg" != "pkg"
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("Pkg", "mod", "1.0.0", 1));

    auto dep = makeRequirement("pkg", "mod", 1); // lowercase
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(!result.success); // case mismatch -> not found
}

TEST_CASE("resolveDependency moduleId case sensitivity", "[version][complex][pkgname]") {
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("pkg", "Acoustic", "1.0.0", 1));

    auto dep = makeRequirement("pkg", "acoustic", 1); // lowercase
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(!result.success);
}

TEST_CASE("resolveDependency empty packageId", "[version][complex][pkgname]") {
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("", "mod", "1.0.0", 1));

    auto dep = makeRequirement("", "mod", 1);
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(result.success);
}

// ===========================================================================
// Version conflicts
// ===========================================================================

TEST_CASE("resolveDependency multiple versions selects highest in range", "[version][complex][conflict]") {
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("pkg", "mod", "1.0.0", 1));
    modules.push_back(makeModule("pkg", "mod", "1.5.0", 1));
    modules.push_back(makeModule("pkg", "mod", "2.0.0", 1));
    modules.push_back(makeModule("pkg", "mod", "3.0.0", 1));

    // Use <=2.0.0 to select from [any, 2.0.0]
    auto dep = makeRequirement("pkg", "mod", 1, "<=2.0.0");
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(result.success);
    // Should select highest <= 2.0.0: 2.0.0
    REQUIRE(result.resolvedVersion == "2.0.0");
}

TEST_CASE("resolveDependency version range empty intersection", "[version][complex][conflict]") {
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("pkg", "mod", "1.0.0", 1));
    modules.push_back(makeModule("pkg", "mod", "2.0.0", 1));

    // No version satisfies this range
    auto dep = makeRequirement("pkg", "mod", 1, ">=3.0.0");
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(!result.success);
    REQUIRE(result.error.find("No version in range") != std::string::npos);
}

TEST_CASE("resolveDependency exact version match", "[version][complex][conflict]") {
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("pkg", "mod", "1.0.0", 1));
    modules.push_back(makeModule("pkg", "mod", "1.5.0", 1));
    modules.push_back(makeModule("pkg", "mod", "2.0.0", 1));

    auto dep = makeRequirement("pkg", "mod", 1, "==1.5.0");
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(result.success);
    REQUIRE(result.resolvedVersion == "1.5.0");
}

TEST_CASE("resolveDependency compatible version range", "[version][complex][conflict]") {
    // ~1.2.0 means >=1.2.0 <1.3.0
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("pkg", "mod", "1.2.0", 1));
    modules.push_back(makeModule("pkg", "mod", "1.2.5", 1));
    modules.push_back(makeModule("pkg", "mod", "1.3.0", 1));
    modules.push_back(makeModule("pkg", "mod", "2.0.0", 1));

    auto dep = makeRequirement("pkg", "mod", 1, "~1.2.0");
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(result.success);
    // Should select 1.2.5 (highest in [1.2.0, 1.3.0))
    REQUIRE(result.resolvedVersion == "1.2.5");
}

TEST_CASE("resolveDependency hyphen range", "[version][complex][conflict]") {
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("pkg", "mod", "1.0.0", 1));
    modules.push_back(makeModule("pkg", "mod", "1.5.0", 1));
    modules.push_back(makeModule("pkg", "mod", "2.0.0", 1));
    modules.push_back(makeModule("pkg", "mod", "2.5.0", 1));

    auto dep = makeRequirement("pkg", "mod", 1, "1.0.0 - 2.0.0");
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(result.success);
    // 2.0.0 is included (hyphen range is inclusive on both ends)
    REQUIRE(result.resolvedVersion == "2.0.0");
}

TEST_CASE("resolveDependency same version different levels", "[version][complex][conflict]") {
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("pkg", "mod", "1.0.0", 1));
    modules.push_back(makeModule("pkg", "mod", "1.0.0", 2)); // same version, different level

    auto dep = makeRequirement("pkg", "mod", 2, "*"); // require level 2
    auto requesting = makeModule("caller", "main", "1.0.0", 2);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(result.success);
    REQUIRE(result.resolvedVersion == "1.0.0");
    REQUIRE(result.resolvedLevel == 2);
}

TEST_CASE("resolveDependency no level match", "[version][complex][conflict]") {
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("pkg", "mod", "1.0.0", 1));
    modules.push_back(makeModule("pkg", "mod", "2.0.0", 1));

    // Request level 2 but only level 1 exists
    auto dep = makeRequirement("pkg", "mod", 2, "*");
    auto requesting = makeModule("caller", "main", "1.0.0", 2);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(!result.success);
}

// ===========================================================================
// Missing dependencies
// ===========================================================================

TEST_CASE("resolveDependency module not found", "[version][complex][missing]") {
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("pkg.a", "mod1", "1.0.0", 1));

    auto dep = makeRequirement("pkg.b", "mod2", 1);
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(!result.success);
    REQUIRE(result.error.find("not found") != std::string::npos);
}

TEST_CASE("resolveDependency empty module list", "[version][complex][missing]") {
    std::vector<ModuleMetadata> modules;

    auto dep = makeRequirement("pkg", "mod", 1);
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(!result.success);
}

TEST_CASE("resolveDependency same package wrong module", "[version][complex][missing]") {
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("pkg", "duration", "1.0.0", 1));
    modules.push_back(makeModule("pkg", "pitch", "1.0.0", 1));

    auto dep = makeRequirement("pkg", "acoustic", 1); // not in package
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(!result.success);
    // Error should list available modules in the package
    REQUIRE(result.error.find("duration") != std::string::npos);
    REQUIRE(result.error.find("pitch") != std::string::npos);
}

TEST_CASE("resolveDependency invalid version range returns error", "[version][complex][missing]") {
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("pkg", "mod", "1.0.0", 1));

    // Invalid version range (operator with empty version)
    auto dep = makeRequirement("pkg", "mod", 1, ">=");
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(!result.success);
}

TEST_CASE("resolveDependency invalid level -1 module", "[version][complex][missing]") {
    std::vector<ModuleMetadata> modules;
    auto m = makeModule("pkg", "mod", "1.0.0", -1); // invalid level
    modules.push_back(m);

    auto dep = makeRequirement("pkg", "mod", 1);
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(!result.success);
    REQUIRE(result.error.find("invalid level") != std::string::npos);
}

// ===========================================================================
// DependencyGraph: cycle detection
// ===========================================================================

TEST_CASE("DependencyGraph detects two-node cycle A->B->A", "[version][complex][cycle]") {
    DependencyGraph graph;

    // A depends on B, B depends on A
    auto modA = makeModule("pkg.a", "mod", "1.0.0", 1);
    modA.resolvedDependencies.push_back(makeResolved("pkg.b", "mod", "1.0.0", 1));

    auto modB = makeModule("pkg.b", "mod", "1.0.0", 1);
    modB.resolvedDependencies.push_back(makeResolved("pkg.a", "mod", "1.0.0", 1));

    graph.addModule(modA);
    graph.addModule(modB);
    graph.buildGraph();

    auto cycles = graph.findCycles();
    REQUIRE(!cycles.empty());
    REQUIRE(cycles[0].size() >= 2);
}

TEST_CASE("DependencyGraph detects three-node cycle A->B->C->A", "[version][complex][cycle]") {
    DependencyGraph graph;

    auto modA = makeModule("pkg.a", "mod", "1.0.0", 1);
    modA.resolvedDependencies.push_back(makeResolved("pkg.b", "mod", "1.0.0", 1));

    auto modB = makeModule("pkg.b", "mod", "1.0.0", 1);
    modB.resolvedDependencies.push_back(makeResolved("pkg.c", "mod", "1.0.0", 1));

    auto modC = makeModule("pkg.c", "mod", "1.0.0", 1);
    modC.resolvedDependencies.push_back(makeResolved("pkg.a", "mod", "1.0.0", 1));

    graph.addModule(modA);
    graph.addModule(modB);
    graph.addModule(modC);
    graph.buildGraph();

    auto cycles = graph.findCycles();
    REQUIRE(!cycles.empty());
    REQUIRE(cycles[0].size() >= 3);
}

TEST_CASE("DependencyGraph detects self-dependency A->A", "[version][complex][cycle]") {
    DependencyGraph graph;

    auto modA = makeModule("pkg.a", "mod", "1.0.0", 1);
    // Self-dependency (buildGraph skips self-edges, so no cycle)
    modA.resolvedDependencies.push_back(makeResolved("pkg.a", "mod", "1.0.0", 1));

    graph.addModule(modA);
    graph.buildGraph();

    // Self-dependency is filtered in buildGraph (depNode != node check)
    auto cycles = graph.findCycles();
    // Self-dependency doesn't form a cycle since the edge is skipped
    REQUIRE(cycles.empty());
}

TEST_CASE("DependencyGraph no cycle in linear chain", "[version][complex][cycle]") {
    DependencyGraph graph;

    auto modA = makeModule("pkg.a", "mod", "1.0.0", 1);
    modA.resolvedDependencies.push_back(makeResolved("pkg.b", "mod", "1.0.0", 1));

    auto modB = makeModule("pkg.b", "mod", "1.0.0", 1);
    modB.resolvedDependencies.push_back(makeResolved("pkg.c", "mod", "1.0.0", 1));

    auto modC = makeModule("pkg.c", "mod", "1.0.0", 1);
    // C has no dependencies

    graph.addModule(modA);
    graph.addModule(modB);
    graph.addModule(modC);
    graph.buildGraph();

    auto cycles = graph.findCycles();
    REQUIRE(cycles.empty());
}

TEST_CASE("DependencyGraph diamond dependency no cycle", "[version][complex][cycle]") {
    // A -> B, A -> C, B -> D, C -> D (diamond, no cycle)
    DependencyGraph graph;

    auto modA = makeModule("pkg.a", "mod", "1.0.0", 1);
    modA.resolvedDependencies.push_back(makeResolved("pkg.b", "mod", "1.0.0", 1));
    modA.resolvedDependencies.push_back(makeResolved("pkg.c", "mod", "1.0.0", 1));

    auto modB = makeModule("pkg.b", "mod", "1.0.0", 1);
    modB.resolvedDependencies.push_back(makeResolved("pkg.d", "mod", "1.0.0", 1));

    auto modC = makeModule("pkg.c", "mod", "1.0.0", 1);
    modC.resolvedDependencies.push_back(makeResolved("pkg.d", "mod", "1.0.0", 1));

    auto modD = makeModule("pkg.d", "mod", "1.0.0", 1);

    graph.addModule(modA);
    graph.addModule(modB);
    graph.addModule(modC);
    graph.addModule(modD);
    graph.buildGraph();

    auto cycles = graph.findCycles();
    REQUIRE(cycles.empty());
}

// ===========================================================================
// DependencyGraph: missing dependency detection
// ===========================================================================

TEST_CASE("DependencyGraph missing dependency buildGraph returns false", "[version][complex][missing]") {
    DependencyGraph graph;

    auto modA = makeModule("pkg.a", "mod", "1.0.0", 1);
    // A depends on B, but B is never added
    modA.resolvedDependencies.push_back(makeResolved("pkg.b", "mod", "1.0.0", 1));

    graph.addModule(modA);
    bool ok = graph.buildGraph();

    // buildGraph returns false when a dependency is missing
    REQUIRE(!ok);
}

TEST_CASE("DependencyGraph partial missing dependency", "[version][complex][missing]") {
    DependencyGraph graph;

    auto modA = makeModule("pkg.a", "mod", "1.0.0", 1);
    modA.resolvedDependencies.push_back(makeResolved("pkg.b", "mod", "1.0.0", 1));
    modA.resolvedDependencies.push_back(makeResolved("pkg.c", "mod", "1.0.0", 1)); // missing

    auto modB = makeModule("pkg.b", "mod", "1.0.0", 1);

    graph.addModule(modA);
    graph.addModule(modB);
    bool ok = graph.buildGraph();

    REQUIRE(!ok); // C is missing
}

// ===========================================================================
// DependencyGraph: initialization order
// ===========================================================================

TEST_CASE("DependencyGraph initialization order linear chain", "[version][complex][order]") {
    DependencyGraph graph;

    auto modA = makeModule("pkg.a", "mod", "1.0.0", 1);
    modA.resolvedDependencies.push_back(makeResolved("pkg.b", "mod", "1.0.0", 1));

    auto modB = makeModule("pkg.b", "mod", "1.0.0", 1);
    modB.resolvedDependencies.push_back(makeResolved("pkg.c", "mod", "1.0.0", 1));

    auto modC = makeModule("pkg.c", "mod", "1.0.0", 1);

    graph.addModule(modA);
    graph.addModule(modB);
    graph.addModule(modC);
    graph.buildGraph();

    auto plans = graph.getPackageInitializationOrder();
    REQUIRE(plans.size() == 3);
    // C must come before B, B before A
    REQUIRE(plans[0].packageId == "pkg.c");
    REQUIRE(plans[1].packageId == "pkg.b");
    REQUIRE(plans[2].packageId == "pkg.a");
}

TEST_CASE("DependencyGraph initialization order diamond", "[version][complex][order]") {
    DependencyGraph graph;

    auto modA = makeModule("pkg.a", "mod", "1.0.0", 1);
    modA.resolvedDependencies.push_back(makeResolved("pkg.b", "mod", "1.0.0", 1));
    modA.resolvedDependencies.push_back(makeResolved("pkg.c", "mod", "1.0.0", 1));

    auto modB = makeModule("pkg.b", "mod", "1.0.0", 1);
    modB.resolvedDependencies.push_back(makeResolved("pkg.d", "mod", "1.0.0", 1));

    auto modC = makeModule("pkg.c", "mod", "1.0.0", 1);
    modC.resolvedDependencies.push_back(makeResolved("pkg.d", "mod", "1.0.0", 1));

    auto modD = makeModule("pkg.d", "mod", "1.0.0", 1);

    graph.addModule(modA);
    graph.addModule(modB);
    graph.addModule(modC);
    graph.addModule(modD);
    graph.buildGraph();

    auto plans = graph.getPackageInitializationOrder();
    REQUIRE(plans.size() == 4);
    // D must come first, A must come last
    REQUIRE(plans[0].packageId == "pkg.d");
    REQUIRE(plans[3].packageId == "pkg.a");
    // B and C can be in either order
    bool bBeforeA = false, cBeforeA = false;
    for (size_t i = 0; i < plans.size(); ++i) {
        if (plans[i].packageId == "pkg.b") bBeforeA = true;
        if (plans[i].packageId == "pkg.c") cBeforeA = true;
        if (plans[i].packageId == "pkg.a") break;
    }
    REQUIRE(bBeforeA);
    REQUIRE(cBeforeA);
}

TEST_CASE("DependencyGraph initialization order empty graph", "[version][complex][order]") {
    DependencyGraph graph;
    graph.buildGraph();
    auto plans = graph.getPackageInitializationOrder();
    REQUIRE(plans.empty());
}

TEST_CASE("DependencyGraph initialization order with cycle returns empty", "[version][complex][order]") {
    DependencyGraph graph;

    auto modA = makeModule("pkg.a", "mod", "1.0.0", 1);
    modA.resolvedDependencies.push_back(makeResolved("pkg.b", "mod", "1.0.0", 1));

    auto modB = makeModule("pkg.b", "mod", "1.0.0", 1);
    modB.resolvedDependencies.push_back(makeResolved("pkg.a", "mod", "1.0.0", 1));

    graph.addModule(modA);
    graph.addModule(modB);
    graph.buildGraph();

    auto plans = graph.getPackageInitializationOrder();
    REQUIRE(plans.empty()); // cycle -> no valid order
}

// ===========================================================================
// DependencyGraph: multi-module packages
// ===========================================================================

TEST_CASE("DependencyGraph multi-module same package", "[version][complex][multi]") {
    // Package has two modules at different levels
    DependencyGraph graph;

    auto mod1 = makeModule("pkg", "duration", "1.0.0", 1);
    auto mod2 = makeModule("pkg", "acoustic", "1.0.0", 1);
    mod2.resolvedDependencies.push_back(makeResolved("pkg", "duration", "1.0.0", 1));

    graph.addModule(mod1);
    graph.addModule(mod2);
    graph.buildGraph();

    auto plans = graph.getPackageInitializationOrder();
    REQUIRE(plans.size() == 1); // single package
    REQUIRE(plans[0].packageId == "pkg");
    REQUIRE(plans[0].initializationOrder.size() == 2);
    // Duration must come before acoustic
    REQUIRE(plans[0].initializationOrder[0].moduleId == "duration");
    REQUIRE(plans[0].initializationOrder[1].moduleId == "acoustic");
}

TEST_CASE("DependencyGraph clear and rebuild", "[version][complex][multi]") {
    DependencyGraph graph;

    graph.addModule(makeModule("pkg.a", "mod", "1.0.0", 1));
    graph.buildGraph();
    REQUIRE(graph.getAllModules().size() == 1);

    graph.clear();
    REQUIRE(graph.getAllModules().empty());

    graph.addModule(makeModule("pkg.b", "mod", "1.0.0", 1));
    graph.buildGraph();
    REQUIRE(graph.getAllModules().size() == 1);
    REQUIRE(graph.getAllModules()[0].packageId == "pkg.b");
}

// ===========================================================================
// Complex multi-package scenario
// ===========================================================================

TEST_CASE("resolveDependency real scenario: G2P + acoustic + vocoder", "[version][complex][scenario]") {
    // Simulate a real DiffSinger package setup:
    // pkg.g2p: g2p module
    // pkg.singer: duration, pitch, variance, acoustic, vocoder modules
    // pkg.singer depends on pkg.g2p
    std::vector<ModuleMetadata> modules;

    // G2P package
    modules.push_back(makeModule("pkg.g2p", "g2p-cmn", "1.0.0", 1));
    modules.push_back(makeModule("pkg.g2p", "g2p-cmn", "1.1.0", 1));
    modules.push_back(makeModule("pkg.g2p", "g2p-cmn", "2.0.0", 1));

    // Singer package
    modules.push_back(makeModule("pkg.singer", "duration", "1.0.0", 1));
    modules.push_back(makeModule("pkg.singer", "acoustic", "1.0.0", 1));
    modules.push_back(makeModule("pkg.singer", "vocoder", "1.0.0", 1));

    // Resolve G2P dependency with compatible range
    auto dep = makeRequirement("pkg.g2p", "g2p-cmn", 1, "~1.0.0");
    auto requesting = makeModule("pkg.singer", "duration", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(result.success);
    // ~1.0.0 means >=1.0.0 <1.1.0, but 1.1.0 has same major.minor (1.1)
    // Actually ~1.0.0 checks major[0]==1, minor[1]==0, patch>=0
    // 1.0.0 matches, 1.1.0 doesn't (minor 1!=0), 2.0.0 doesn't
    REQUIRE(result.resolvedVersion == "1.0.0");
}

TEST_CASE("resolveDependency real scenario: multi-level acoustic", "[version][complex][scenario]") {
    // Some packages have multiple levels of the same module
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("pkg.singer", "acoustic", "1.0.0", 1));
    modules.push_back(makeModule("pkg.singer", "acoustic", "1.0.0", 2));
    modules.push_back(makeModule("pkg.singer", "acoustic", "1.0.0", 3));

    // Level 1 requesting level 2
    auto dep = makeRequirement("pkg.singer", "acoustic", 2);
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(result.success);
    REQUIRE(result.resolvedLevel == 2);
}

TEST_CASE("resolveDependency star range matches all", "[version][complex][conflict]") {
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("pkg", "mod", "0.9.0", 1));
    modules.push_back(makeModule("pkg", "mod", "1.0.0", 1));
    modules.push_back(makeModule("pkg", "mod", "2.0.0", 1));

    auto dep = makeRequirement("pkg", "mod", 1, "*");
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(result.success);
    REQUIRE(result.resolvedVersion == "2.0.0"); // highest
}

TEST_CASE("resolveDependency empty versionRange matches all", "[version][complex][conflict]") {
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("pkg", "mod", "1.0.0", 1));
    modules.push_back(makeModule("pkg", "mod", "3.0.0", 1));

    auto dep = makeRequirement("pkg", "mod", 1, ""); // empty = any
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(result.success);
    REQUIRE(result.resolvedVersion == "3.0.0");
}

// ===========================================================================
// Extreme cases: resolver-level edge inputs
// ===========================================================================

TEST_CASE("resolveDependency dedupes duplicate versions", "[version][complex][extreme]") {
    // Two identical module entries (same id+version+level) must not produce
    // duplicate candidates; the resolver dedupes via sort+unique.
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("pkg", "mod", "1.0.0", 1));
    modules.push_back(makeModule("pkg", "mod", "1.0.0", 1)); // exact duplicate

    auto dep = makeRequirement("pkg", "mod", 1, "*");
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(result.success);
    REQUIRE(result.resolvedVersion == "1.0.0");
    REQUIRE(result.candidates.size() == 1);
    REQUIRE(result.candidates[0] == "1.0.0");
}

TEST_CASE("resolveDependency level -1 matches requesting level", "[version][complex][extreme]") {
    // When dependency.level == -1 (any), the resolver falls back to the
    // REQUESTING module's level and selects versions whose level matches it.
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("pkg", "mod", "1.0.0", 1));
    modules.push_back(makeModule("pkg", "mod", "2.0.0", 2));

    auto dep = makeRequirement("pkg", "mod", -1, "*"); // any level
    auto requesting = makeModule("caller", "main", "1.0.0", 1); // requesting level 1
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(result.success);
    // 1.0.0 is the only version at level 1 (the requester's level).
    REQUIRE(result.resolvedVersion == "1.0.0");
    REQUIRE(result.resolvedLevel == 1);
}

TEST_CASE("resolveDependency level -1 with no compatible level fails", "[version][complex][extreme]") {
    // Requesting level 1 but the only candidate is at level 2: with
    // dependency.level == -1 the fallback to the requester's level yields no
    // match.
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("pkg", "mod", "1.0.0", 2));

    auto dep = makeRequirement("pkg", "mod", -1, "*");
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(!result.success);
    REQUIRE(result.error.find("compatible level") != std::string::npos);
}

TEST_CASE("resolveDependency version 0.0.0 is selectable", "[version][complex][extreme]") {
    // "0.0.0" is a legitimate version and must not be treated as empty or
    // skipped by selectHighestVersion.
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("pkg", "mod", "0.0.0", 1));

    auto dep = makeRequirement("pkg", "mod", 1, "*");
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(result.success);
    REQUIRE(result.resolvedVersion == "0.0.0");
}

TEST_CASE("resolveDependency multiple versions selects highest with star", "[version][complex][extreme]") {
    // Stress: many versions with "*" range picks the numerically highest,
    // not the lexicographically last string.
    std::vector<ModuleMetadata> modules;
    modules.push_back(makeModule("pkg", "mod", "1.10.0", 1));
    modules.push_back(makeModule("pkg", "mod", "1.2.0", 1));
    modules.push_back(makeModule("pkg", "mod", "1.9.0", 1));

    auto dep = makeRequirement("pkg", "mod", 1, "*");
    auto requesting = makeModule("caller", "main", "1.0.0", 1);
    auto result = VersionResolver::resolveDependency(modules, dep, requesting);

    REQUIRE(result.success);
    // 1.10.0 > 1.9.0 > 1.2.0 numerically (lexicographically "1.10.0" < "1.2.0"
    // would be wrong). Verifies numeric, not string, comparison.
    REQUIRE(result.resolvedVersion == "1.10.0");
}
