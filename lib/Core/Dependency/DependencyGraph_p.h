#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <synthrt/Core/Dependency/DependencyGraph.h>

namespace srt::dependency {

    // Impl 为独立式（非继承 NamedObject::Impl）：DependencyGraph 不继承 NamedObject。
    class DependencyGraph::Impl {
    public:
        Impl();
        ~Impl();

        bool addModule(const ModuleMetadata &module);
        bool buildGraph();
        void clear();

        std::vector<std::vector<ModuleMetadata>> getCycles() const;
        std::vector<ModuleMetadata> getAllModules() const;
        std::vector<PackageInitializationPlan> getPackageInitializationOrder() const;

    private:
        struct GraphNode {
            ModuleMetadata module;
            std::vector<std::shared_ptr<GraphNode>> neighbors;
            explicit GraphNode(ModuleMetadata mod) : module(std::move(mod)) {}
        };

        using NodeMap = std::unordered_map<std::string, std::shared_ptr<GraphNode>>;
        using MainModuleMap = std::unordered_map<ModuleMetadata, std::vector<std::shared_ptr<GraphNode>>,
                                                 ModuleMetadata::MainModuleHash, ModuleMetadata::MainModuleEqual>;

        std::shared_ptr<GraphNode> getOrCreateNode(const ModuleMetadata &module);
        std::vector<std::shared_ptr<GraphNode>> findNodesByDependency(const ResolvedDependency &dep);

        /// Kahn's topological sort. Returns sorted modules, or empty if cycle detected.
        /// If a cycle is detected and \p cycleMembers is non-null, the modules in the
        /// cycle are written there.
        std::vector<ModuleMetadata> getGlobalModuleInitializationOrder(
            std::vector<ModuleMetadata> *cycleMembers = nullptr) const;

        NodeMap nodeMap;
        MainModuleMap mainModuleMap;
        bool graphBuilt = false;
    };

} // namespace srt::dependency
