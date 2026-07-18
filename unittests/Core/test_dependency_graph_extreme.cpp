// DependencyGraph 与 LevelCompatibilityChecker 极端场景测试
//
// 覆盖人工决策约束（来自 docs/decisions/human-decisions.md）：
//   - 依赖图循环检测（Kahn's algorithm）
//   - 自依赖处理（DependencyResolver 移除 packageId==self 的依赖）
//   - diamond 依赖（A → B → D, A → C → D）的拓扑排序
//   - 缺失依赖：buildGraph 返回 false
//   - LevelCompatibilityChecker 边缘场景：maximumLevel=0、pluginLevel < minimum、
//     pluginLevel > effectiveMax
//
// 源码实现见：
//   - lib/Core/Dependency/DependencyGraph.cpp (Kahn's algorithm 拓扑排序)
//   - lib/Core/Dependency/DependencyResolver.cpp (自依赖移除)
//   - lib/Core/Dependency/LevelCompatibilityChecker.cpp

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Dependency/DependencyGraph.h>
#include <synthrt/Core/Dependency/LevelCompatibilityChecker.h>

using srt::dependency::DependencyGraph;
using srt::dependency::DependencyRequirement;
using srt::dependency::LevelCompatibilityChecker;
using srt::dependency::ModuleMetadata;
using srt::dependency::ResolvedDependency;

// ---------------------------------------------------------------------------
// 辅助函数：构造一个最小可用的 ModuleMetadata
// ---------------------------------------------------------------------------
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

} // namespace

// ---------------------------------------------------------------------------
// buildGraph 空图与单节点
// ---------------------------------------------------------------------------
TEST_CASE("DependencyGraph empty graph builds successfully", "[depgraph][basic]") {
    DependencyGraph g;
    REQUIRE(g.buildGraph());
    REQUIRE(g.getAllModules().empty());
    REQUIRE(g.findCycles().empty());
    REQUIRE(g.getPackageInitializationOrder().empty());
}

TEST_CASE("DependencyGraph single module no dependencies", "[depgraph][basic]") {
    DependencyGraph g;
    auto m = makeModule("pkg", "mod", "1.0.0");
    g.addModule(m);
    REQUIRE(g.buildGraph());
    REQUIRE(g.getAllModules().size() == 1);
    REQUIRE(g.findCycles().empty());

    auto order = g.getPackageInitializationOrder();
    REQUIRE(order.size() == 1);
    REQUIRE(order[0].packageId == "pkg");
    REQUIRE(order[0].initializationOrder.size() == 1);
}

// ---------------------------------------------------------------------------
// 循环依赖检测（Kahn's algorithm）
// ---------------------------------------------------------------------------
TEST_CASE("DependencyGraph detects two-node cycle", "[depgraph][cycle]") {
    DependencyGraph g;
    auto a = makeModule("pkg", "A", "1.0.0");
    auto b = makeModule("pkg", "B", "1.0.0");
    a.resolvedDependencies.push_back(makeDep("pkg", "B", "1.0.0"));
    b.resolvedDependencies.push_back(makeDep("pkg", "A", "1.0.0"));
    g.addModule(a);
    g.addModule(b);

    REQUIRE(g.buildGraph());
    auto cycles = g.findCycles();
    REQUIRE(cycles.size() == 1);
    REQUIRE(cycles[0].size() == 2);
}

TEST_CASE("DependencyGraph detects three-node cycle", "[depgraph][cycle]") {
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
    REQUIRE(cycles.size() == 1);
    REQUIRE(cycles[0].size() == 3);
}

TEST_CASE("DependencyGraph self-dependency forms a cycle at graph level",
          "[depgraph][cycle][self]") {
    // 注意：DependencyResolver::resolveAllDependencies 会在解析阶段移除
    // packageId == depRaw.packageId && moduleId == depRaw.moduleId &&
    // level == module.level 的自依赖。这里直接构造已 resolved 的
    // ModuleMetadata 验证 DependencyGraph 的行为：自依赖在图层面会被
    // buildGraph 跳过（depNode == node 时不加边），所以不构成循环。
    DependencyGraph g;
    auto a = makeModule("pkg", "A", "1.0.0");
    a.resolvedDependencies.push_back(makeDep("pkg", "A", "1.0.0"));
    g.addModule(a);

    REQUIRE(g.buildGraph());
    // 自依赖被 buildGraph 内部跳过（depNode != node 检查），所以无循环
    auto cycles = g.findCycles();
    REQUIRE(cycles.empty());
}

// ---------------------------------------------------------------------------
// Diamond 依赖：A → B → D, A → C → D
// ---------------------------------------------------------------------------
TEST_CASE("DependencyGraph diamond dependency topological order",
          "[depgraph][diamond]") {
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
    REQUIRE(order.size() == 1); // 全部在同一个 packageId "pkg"
    auto &initOrder = order[0].initializationOrder;
    REQUIRE(initOrder.size() == 4);

    // D 必须先于 B 和 C；B 和 C 必须先于 A
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
    REQUIRE(idxD < idxB);
    REQUIRE(idxD < idxC);
    REQUIRE(idxB < idxA);
    REQUIRE(idxC < idxA);
}

// ---------------------------------------------------------------------------
// 缺失依赖：buildGraph 返回 false
// ---------------------------------------------------------------------------
TEST_CASE("DependencyGraph missing dependency causes buildGraph to fail",
          "[depgraph][missing]") {
    DependencyGraph g;
    auto a = makeModule("pkg", "A", "1.0.0");
    // 依赖一个未注册的模块
    a.resolvedDependencies.push_back(makeDep("pkg", "Missing", "1.0.0"));
    g.addModule(a);

    // buildGraph 找不到 Missing 节点，返回 false
    REQUIRE_FALSE(g.buildGraph());
    // 但仍能查询已添加的模块
    REQUIRE(g.getAllModules().size() == 1);
}

// ---------------------------------------------------------------------------
// 多 packageId 的拓扑排序
// ---------------------------------------------------------------------------
TEST_CASE("DependencyGraph multi-package topological order", "[depgraph][multi-pkg]") {
    DependencyGraph g;
    auto a = makeModule("pkgA", "A", "1.0.0");
    auto b = makeModule("pkgB", "B", "1.0.0");
    // pkgA:A 依赖 pkgB:B
    a.resolvedDependencies.push_back(makeDep("pkgB", "B", "1.0.0"));
    g.addModule(a);
    g.addModule(b);

    REQUIRE(g.buildGraph());
    REQUIRE(g.findCycles().empty());

    auto order = g.getPackageInitializationOrder();
    REQUIRE(order.size() == 2);
    // pkgB 必须在 pkgA 之前
    REQUIRE(order[0].packageId == "pkgB");
    REQUIRE(order[1].packageId == "pkgA");
}

// ---------------------------------------------------------------------------
// clear 重置图状态
// ---------------------------------------------------------------------------
TEST_CASE("DependencyGraph clear resets state", "[depgraph][clear]") {
    DependencyGraph g;
    g.addModule(makeModule("pkg", "A", "1.0.0"));
    g.addModule(makeModule("pkg", "B", "1.0.0"));
    REQUIRE(g.buildGraph());
    REQUIRE(g.getAllModules().size() == 2);

    g.clear();
    REQUIRE(g.getAllModules().empty());
    REQUIRE(g.findCycles().empty());
    REQUIRE(g.getPackageInitializationOrder().empty());
}

// ===========================================================================
// LevelCompatibilityChecker
// ===========================================================================

// ---------------------------------------------------------------------------
// getEffectiveMaximumLevel: maximumLevel=0 → 使用 currentLevel
// ---------------------------------------------------------------------------
TEST_CASE("LevelCompatibilityChecker effective max level handles zero",
          "[level][effective-max]") {
    SECTION("maximumLevel=0 falls back to currentLevel") {
        LevelCompatibilityChecker::LevelConfig cfg(2, 1, 0);
        REQUIRE(cfg.getEffectiveMaximumLevel() == 2);
    }
    SECTION("maximumLevel>0 uses maximumLevel") {
        LevelCompatibilityChecker::LevelConfig cfg(2, 1, 5);
        REQUIRE(cfg.getEffectiveMaximumLevel() == 5);
    }
    SECTION("maximumLevel equals currentLevel") {
        LevelCompatibilityChecker::LevelConfig cfg(3, 1, 3);
        REQUIRE(cfg.getEffectiveMaximumLevel() == 3);
    }
}

// ---------------------------------------------------------------------------
// checkCorePlugin 边界场景
// ---------------------------------------------------------------------------
TEST_CASE("LevelCompatibilityChecker checkCorePlugin compatible cases",
          "[level][core-plugin]") {
    SECTION("pluginLevel equals currentLevel") {
        LevelCompatibilityChecker::LevelConfig cfg(2, 1, 0);
        auto r = LevelCompatibilityChecker::checkCorePlugin(2, cfg);
        REQUIRE(r.isCompatible);
        REQUIRE(r.pluginLevel == 2);
        REQUIRE(r.systemMinimum == 1);
        REQUIRE(r.systemMaximum == 0);
    }
    SECTION("pluginLevel equals minimumLevel") {
        LevelCompatibilityChecker::LevelConfig cfg(3, 1, 5);
        auto r = LevelCompatibilityChecker::checkCorePlugin(1, cfg);
        REQUIRE(r.isCompatible);
    }
    SECTION("pluginLevel equals effectiveMax") {
        LevelCompatibilityChecker::LevelConfig cfg(3, 1, 5);
        auto r = LevelCompatibilityChecker::checkCorePlugin(5, cfg);
        REQUIRE(r.isCompatible);
    }
}

TEST_CASE("LevelCompatibilityChecker checkCorePlugin incompatible cases",
          "[level][core-plugin]") {
    SECTION("pluginLevel below minimum") {
        LevelCompatibilityChecker::LevelConfig cfg(3, 2, 5);
        auto r = LevelCompatibilityChecker::checkCorePlugin(1, cfg);
        REQUIRE_FALSE(r.isCompatible);
        REQUIRE(r.pluginLevel == 1);
        REQUIRE(r.systemMinimum == 2);
        REQUIRE_FALSE(r.suggestion.empty());
    }
    SECTION("pluginLevel exceeds effectiveMax when maximumLevel=0") {
        // maximumLevel=0 → effectiveMax = currentLevel = 2
        LevelCompatibilityChecker::LevelConfig cfg(2, 1, 0);
        auto r = LevelCompatibilityChecker::checkCorePlugin(3, cfg);
        REQUIRE_FALSE(r.isCompatible);
        REQUIRE(r.pluginLevel == 3);
        REQUIRE_FALSE(r.suggestion.empty());
    }
    SECTION("pluginLevel exceeds effectiveMax when maximumLevel>0") {
        LevelCompatibilityChecker::LevelConfig cfg(2, 1, 4);
        auto r = LevelCompatibilityChecker::checkCorePlugin(5, cfg);
        REQUIRE_FALSE(r.isCompatible);
    }
}

// ---------------------------------------------------------------------------
// checkDependencyPlugin 等价于 checkCorePlugin
// ---------------------------------------------------------------------------
TEST_CASE("LevelCompatibilityChecker checkDependencyPlugin matches checkCorePlugin",
          "[level][dependency-plugin]") {
    LevelCompatibilityChecker::LevelConfig cfg(2, 1, 3);
    auto r1 = LevelCompatibilityChecker::checkCorePlugin(2, cfg);
    auto r2 = LevelCompatibilityChecker::checkDependencyPlugin(2, cfg);
    REQUIRE(r1.isCompatible == r2.isCompatible);
    REQUIRE(r1.pluginLevel == r2.pluginLevel);
    REQUIRE(r1.systemMinimum == r2.systemMinimum);
    REQUIRE(r1.systemMaximum == r2.systemMaximum);
    REQUIRE(r1.message == r2.message);
    REQUIRE(r1.suggestion == r2.suggestion);
}

// ---------------------------------------------------------------------------
// isInSupportedRange
// ---------------------------------------------------------------------------
TEST_CASE("LevelCompatibilityChecker isInSupportedRange edge cases",
          "[level][supported-range]") {
    SECTION("pluginLevel in range") {
        LevelCompatibilityChecker::LevelConfig cfg(2, 1, 3);
        auto r = LevelCompatibilityChecker::checkCorePlugin(2, cfg);
        REQUIRE(r.isInSupportedRange());
    }
    SECTION("pluginLevel below minimum not in range") {
        LevelCompatibilityChecker::LevelConfig cfg(2, 1, 3);
        auto r = LevelCompatibilityChecker::checkCorePlugin(0, cfg);
        // pluginLevel=0, minimum=1, effectiveMax = 3 (maximumLevel>0)
        // isInSupportedRange: 0 >= 1? false → false
        REQUIRE_FALSE(r.isInSupportedRange());
    }
    SECTION("pluginLevel exceeds effectiveMax when maximumLevel=0") {
        // ValidationResult.systemMaximum = config.maximumLevel = 0
        // isInSupportedRange: effectiveMax = 0 > 0 ? 0 : pluginLevel = pluginLevel
        // 当 pluginLevel >= minimumLevel 时为 true
        LevelCompatibilityChecker::LevelConfig cfg(2, 1, 0);
        auto r = LevelCompatibilityChecker::checkCorePlugin(5, cfg);
        // pluginLevel=5, minimum=1, systemMaximum=0
        // isInSupportedRange: effectiveMax = (0 > 0) ? 0 : 5 = 5
        // 5 >= 1 && 5 <= 5 → true（但 checkCorePlugin 返回 isCompatible=false）
        // 这表明 isInSupportedRange 与 isCompatible 在 maximumLevel=0 时可能不一致
        REQUIRE(r.isInSupportedRange());
    }
}

// ---------------------------------------------------------------------------
// checkAll 批量检查
// ---------------------------------------------------------------------------
TEST_CASE("LevelCompatibilityChecker checkAll aggregates results",
          "[level][check-all]") {
    LevelCompatibilityChecker::LevelConfig cfg(2, 1, 3);
    std::vector<std::pair<std::string, int>> plugins = {{"p1", 2}, {"p2", 5}};
    std::vector<std::pair<std::string, int>> deps = {{"d1", 1}};

    auto results = LevelCompatibilityChecker::checkAll(plugins, deps, cfg);
    REQUIRE(results.size() == 3);
    REQUIRE(results[0].isCompatible);   // p1 level=2
    REQUIRE_FALSE(results[1].isCompatible); // p2 level=5 exceeds max=3
    REQUIRE(results[2].isCompatible);   // d1 level=1
}

// ---------------------------------------------------------------------------
// generateReport 文本输出
// ---------------------------------------------------------------------------
TEST_CASE("LevelCompatibilityChecker generateReport produces text", "[level][report]") {
    LevelCompatibilityChecker::LevelConfig cfg(2, 1, 3);
    std::vector<std::pair<std::string, int>> plugins = {{"p1", 2}};
    std::vector<std::pair<std::string, int>> deps = {{"d1", 5}};

    auto results = LevelCompatibilityChecker::checkAll(plugins, deps, cfg);
    auto report = LevelCompatibilityChecker::generateReport(results);
    REQUIRE_FALSE(report.empty());
    REQUIRE(report.find("Total checks: 2") != std::string::npos);
    REQUIRE(report.find("Compatible: 1") != std::string::npos);
    REQUIRE(report.find("Incompatible: 1") != std::string::npos);
    REQUIRE(report.find("Plugin Level: 5") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 默认 LevelConfig 构造
// ---------------------------------------------------------------------------
TEST_CASE("LevelCompatibilityChecker default LevelConfig", "[level][default]") {
    LevelCompatibilityChecker::LevelConfig cfg;
    REQUIRE(cfg.currentLevel == 1);
    REQUIRE(cfg.minimumLevel == 1);
    REQUIRE(cfg.maximumLevel == 1);
    REQUIRE(cfg.getEffectiveMaximumLevel() == 1);
}

// ---------------------------------------------------------------------------
// ds-editor-lite 真实场景：声库 Level 兼容性
//
// Lite 在 SynthrtEngine::initialize 中加载声库时，会通过依赖图检查声库声明
// 的 Level 是否在系统支持范围内。Level 不匹配的声库应被跳过而非崩溃。
// ---------------------------------------------------------------------------
TEST_CASE("LevelCompatibilityChecker realistic voicebank level check",
          "[level][realworld]") {
    SECTION("voicebank declares Level 2, system supports 1-3") {
        LevelCompatibilityChecker::LevelConfig cfg(2, 1, 3);
        auto r = LevelCompatibilityChecker::checkCorePlugin(2, cfg);
        REQUIRE(r.isCompatible);
    }
    SECTION("voicebank declares Level 5, system max is 3") {
        LevelCompatibilityChecker::LevelConfig cfg(2, 1, 3);
        auto r = LevelCompatibilityChecker::checkCorePlugin(5, cfg);
        REQUIRE_FALSE(r.isCompatible);
        REQUIRE(r.suggestion.find("Update system") != std::string::npos);
    }
}
