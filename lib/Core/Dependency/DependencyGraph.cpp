#include <synthrt/Core/Dependency/DependencyGraph.h>
#include <synthrt/Core/Dependency/VersionUtils.h>
#include "DependencyGraph_p.h"

#include <queue>
#include <unordered_set>
#include <utility>

// TODO: Re-enable logging when srt::core::Logger is migrated (see docs/refactoring-v3/07-engineering-conventions.md §6)
// #include <LangCore/Core/ManagerLogger.h>

namespace srt::dependency {

    bool DependencyRequirement::operator==(const DependencyRequirement &other) const {
        return packageId == other.packageId && moduleId == other.moduleId && level == other.level &&
            versionRange == other.versionRange;
    }

    std::string DependencyRequirement::key() const { return packageId + ":" + moduleId + ":" + std::to_string(level); }

    bool ResolvedDependency::operator==(const ResolvedDependency &other) const {
        return packageId == other.packageId && moduleId == other.moduleId && level == other.level &&
            version == other.version;
    }

    std::string ResolvedDependency::key() const {
        return packageId + ":" + moduleId + ":" + version + ":" + std::to_string(level);
    }

    bool ModuleMetadata::isSameMainModule(const ModuleMetadata &other) const {
        return context == other.context && contextVersion == other.contextVersion &&
            moduleId == other.moduleId && iid == other.iid && type == other.type &&
            configuration == other.configuration && level == other.level;
    }

    bool ModuleMetadata::operator==(const ModuleMetadata &other) const {
        return context == other.context && contextVersion == other.contextVersion &&
            packageId == other.packageId && moduleId == other.moduleId &&
            iid == other.iid && type == other.type && configuration == other.configuration &&
            version == other.version && level == other.level;
    }

    std::string ModuleMetadata::key() const {
        std::string ctxPart = context;
        if (!contextVersion.isEmpty())
            ctxPart += "@" + contextVersion.toString();
        return ctxPart + ":" + packageId + ":" + moduleId + ":" + version + ":" + iid + ":" + type + ":" +
            configuration + ":" + std::to_string(level);
    }

    std::string ModuleMetadata::uniqueKey() const {
        std::string ctxPart = context;
        if (!contextVersion.isEmpty())
            ctxPart += "@" + contextVersion.toString();
        return ctxPart + ":" + packageId + ":" + moduleId + ":" + iid + ":" + type + ":" + std::to_string(level);
    }

    size_t ModuleMetadata::MainModuleHash::operator()(const ModuleMetadata &info) const {
        const size_t h0 = std::hash<std::string>()(info.context);
        const size_t hv = std::hash<stdc::VersionNumber>()(info.contextVersion);
        const size_t h1 = std::hash<std::string>()(info.moduleId);
        const size_t h2 = std::hash<std::string>()(info.iid);
        const size_t h3 = std::hash<std::string>()(info.type);
        const size_t h4 = std::hash<std::string>()(info.configuration);
        const size_t h5 = std::hash<int>()(info.level);
        return h0 ^ hv << 1 ^ h1 << 2 ^ h2 << 3 ^ h3 << 4 ^ h4 << 5 ^ h5 << 6;
    }

    bool ModuleMetadata::MainModuleEqual::operator()(const ModuleMetadata &a, const ModuleMetadata &b) const {
        return a.isSameMainModule(b);
    }

    DependencyGraph::Impl::Impl() = default;
    DependencyGraph::Impl::~Impl() = default;

    bool DependencyGraph::Impl::addModule(const ModuleMetadata &module) {
        const auto node = getOrCreateNode(module);
        return true;
    }

    bool DependencyGraph::Impl::buildGraph() {
        if (m_graphBuilt) {
            // TODO: Re-enable logging when srt::core::Logger is migrated
            // MgrLog.langCoreWarning("Graph already built");
            return true;
        }

        bool success = true;

        for (const auto &[key, node] : m_nodeMap) {
            for (const auto &dep : node->module.resolvedDependencies) {
                auto depNodes = findNodesByDependency(dep);

                if (depNodes.empty()) {
                    // TODO: Re-enable logging when srt::core::Logger is migrated
                    // MgrLog.langCoreCritical("Error: Cannot add module %1:%2 (v%3, level %4)", node->module.packageId,
                    //                                        node->module.moduleId, node->module.version, node->module.level);
                    // MgrLog.langCoreCritical("  Missing dependency: %1:%2 (v%3, level %4)", dep.packageId, dep.moduleId,
                    //                                        dep.version, dep.level);
                    // MgrLog.langCoreCritical("  Available modules in graph:");
                    // for (const auto &[existingKey, existingNode] : m_nodeMap) {
                    //     MgrLog.langCoreCritical("    - %1:%2 (v%3, level %4)", existingNode->module.packageId,
                    //                             existingNode->module.moduleId, existingNode->module.version,
                    //                             existingNode->module.level);
                    // }

                    success = false;
                    continue;
                }

                for (const auto &depNode : depNodes) {
                    if (depNode != node) {
                        bool alreadyExists = false;
                        for (const auto &neighbor : node->neighbors) {
                            if (neighbor == depNode) {
                                alreadyExists = true;
                                break;
                            }
                        }

                        if (!alreadyExists) {
                            node->neighbors.push_back(depNode);
                        }
                    }
                }
            }
        }

        m_graphBuilt = success;
        return success;
    }

    std::shared_ptr<DependencyGraph::Impl::GraphNode>
    DependencyGraph::Impl::getOrCreateNode(const ModuleMetadata &module) {
        const auto key = module.key();
        if (const auto it = m_nodeMap.find(key); it != m_nodeMap.end())
            return it->second;

        auto node = std::make_shared<GraphNode>(module);
        m_nodeMap[key] = node;
        m_mainModuleMap[module].push_back(node);
        return node;
    }

    void DependencyGraph::Impl::clear() {
        // Break reference-count cycles (A<->B mutual edges) before releasing
        // the map entries; otherwise shared_ptr cycles keep nodes alive.
        for (auto &kv : m_nodeMap) {
            kv.second->neighbors.clear();
        }
        m_nodeMap.clear();
        m_mainModuleMap.clear();
        m_graphBuilt = false;
    }

    std::vector<std::vector<ModuleMetadata>> DependencyGraph::Impl::getCycles() const {
        // Use Kahn's algorithm over the already-built graph to detect cycle members:
        // modules remaining after topological sort exhaustion are in cycles.
        std::unordered_map<std::string, int> inDegree;

        for (const auto &[key, node] : m_nodeMap) {
            inDegree[key] = 0;
        }

        // Compute in-degrees from the existing neighbor edges
        for (const auto &[key, node] : m_nodeMap) {
            for (const auto &neighbor : node->neighbors) {
                inDegree[neighbor->module.key()]++;
            }
        }

        // Kahn's: remove zero-in-degree nodes iteratively
        std::queue<std::string> q;
        for (const auto &[key, degree] : inDegree) {
            if (degree == 0)
                q.push(key);
        }
        std::unordered_set<std::string> visited;
        while (!q.empty()) {
            auto key = q.front();
            q.pop();
            visited.insert(key);
            auto nodeIt = m_nodeMap.find(key);
            if (nodeIt != m_nodeMap.end()) {
                for (const auto &neighbor : nodeIt->second->neighbors) {
                    auto neighborKey = neighbor->module.key();
                    if (--inDegree[neighborKey] == 0)
                        q.push(neighborKey);
                }
            }
        }

        // Remaining nodes are in cycles
        if (visited.size() == m_nodeMap.size())
            return {};

        // Collect all unvisited modules as a single cycle group
        std::vector<ModuleMetadata> cycleMembers;
        for (const auto &[key, node] : m_nodeMap) {
            if (visited.find(key) == visited.end()) {
                cycleMembers.push_back(node->module);
            }
        }
        if (!cycleMembers.empty())
            return {std::move(cycleMembers)};
        return {};
    }

    std::vector<ModuleMetadata> DependencyGraph::Impl::getGlobalModuleInitializationOrder(
        std::vector<ModuleMetadata> *cycleMembers) const {

        // Reuse the already-built neighbor edges from buildGraph() instead of
        // rebuilding the adjacency structure from resolvedDependencies.
        std::unordered_map<std::string, int> inDegree;

        for (const auto &[key, node] : m_nodeMap) {
            inDegree[key] = 0;
        }

        // node->neighbors are the nodes that *depend on* node (i.e. node is a dependency
        // of its neighbors). The edge direction in buildGraph() is: dependency -> dependent.
        // Actually, looking at buildGraph(), node->neighbors stores the dependencies of node,
        // not the dependents. So for topological sort (dependencies first), the edge direction
        // is: node depends on neighbor, so neighbor should come first.
        // In-degree should count how many dependencies a node has (how many edges point TO it
        // in the "dependent -> dependency" direction). But for Kahn's we need the reverse:
        // edges go dependency -> dependent, and we process zero-in-degree first.
        //
        // buildGraph() stores: for each node, its neighbors = the nodes it depends on.
        // So the edge is: node -> neighbor (meaning "node depends on neighbor").
        // For topological sort (dependencies first), we need to reverse:
        // neighbor -> node means "neighbor must come before node".
        // In-degree of node = number of its dependencies = node->neighbors.size().

        for (const auto &[key, node] : m_nodeMap) {
            inDegree[key] = static_cast<int>(node->neighbors.size());
        }

        // Build reverse adjacency: for each dependency, track which nodes depend on it.
        std::unordered_map<std::string, std::vector<const GraphNode *>> reverseDeps;
        for (const auto &[key, node] : m_nodeMap) {
            for (const auto &neighbor : node->neighbors) {
                reverseDeps[neighbor->module.key()].push_back(node.get());
            }
        }

        std::vector<ModuleMetadata> order;
        std::queue<const GraphNode *> zeroInDegreeNodes;

        for (const auto &[key, node] : m_nodeMap) {
            if (inDegree[key] == 0) {
                zeroInDegreeNodes.push(node.get());
            }
        }

        while (!zeroInDegreeNodes.empty()) {
            const auto node = zeroInDegreeNodes.front();
            zeroInDegreeNodes.pop();
            order.push_back(node->module);

            auto rIt = reverseDeps.find(node->module.key());
            if (rIt != reverseDeps.end()) {
                for (const auto *dependent : rIt->second) {
                    if (--inDegree[dependent->module.key()] == 0) {
                        zeroInDegreeNodes.push(dependent);
                    }
                }
            }
        }

        if (order.size() != m_nodeMap.size()) {
            // Cycle detected — collect remaining nodes
            if (cycleMembers) {
                std::unordered_set<std::string> visited;
                for (const auto &m : order) visited.insert(m.key());
                for (const auto &[key, node] : m_nodeMap) {
                    if (visited.find(key) == visited.end())
                        cycleMembers->push_back(node->module);
                }
            }
            return {};
        }

        return order;
    }

    std::vector<PackageInitializationPlan> DependencyGraph::Impl::getPackageInitializationOrder() const {
        const auto globalOrder = getGlobalModuleInitializationOrder();
        if (globalOrder.empty())
            return {};

        std::unordered_map<std::string, PackageInitializationPlan> packageMap;
        for (const auto &module : globalOrder) {
            auto &[packageId, packagePath, initializationOrder] = packageMap[module.packageId];
            packageId = module.packageId;
            packagePath = module.packagePath;
            initializationOrder.push_back(module);
        }

        std::unordered_set<std::string> seenPackages;
        std::vector<PackageInitializationPlan> sortedResult;
        sortedResult.reserve(packageMap.size());

        for (const auto &module : globalOrder) {
            if (seenPackages.insert(module.packageId).second) {
                sortedResult.push_back(packageMap[module.packageId]);
            }
        }

        return sortedResult;
    }

    std::vector<ModuleMetadata> DependencyGraph::Impl::getAllModules() const {
        std::vector<ModuleMetadata> modules;
        modules.reserve(m_nodeMap.size());
        for (const auto &[_, node] : m_nodeMap)
            modules.push_back(node->module);
        return modules;
    }

    std::vector<std::shared_ptr<DependencyGraph::Impl::GraphNode>>
    DependencyGraph::Impl::findNodesByDependency(const ResolvedDependency &dep) {
        std::vector<std::shared_ptr<GraphNode>> result;

        for (auto &[_, node] : m_nodeMap) {
            if (node->module.packageId == dep.packageId && node->module.moduleId == dep.moduleId &&
                node->module.level == dep.level && node->module.version == dep.version) {
                result.push_back(node);
            }
        }

        return result;
    }

    DependencyGraph::DependencyGraph() : _impl(std::make_unique<Impl>()) {}
    DependencyGraph::~DependencyGraph() = default;
    DependencyGraph::DependencyGraph(DependencyGraph &&) noexcept = default;
    DependencyGraph &DependencyGraph::operator=(DependencyGraph &&) noexcept = default;

    void DependencyGraph::addModule(const ModuleMetadata &module) { _impl->addModule(module); }

    bool DependencyGraph::buildGraph() { return _impl->buildGraph(); }

    void DependencyGraph::clear() { _impl->clear(); }

    std::vector<std::vector<ModuleMetadata>> DependencyGraph::findCycles() const { return _impl->getCycles(); }

    std::vector<ModuleMetadata> DependencyGraph::getAllModules() const { return _impl->getAllModules(); }

    std::vector<PackageInitializationPlan> DependencyGraph::getPackageInitializationOrder() const {
        return _impl->getPackageInitializationOrder();
    }
} // namespace srt::dependency
