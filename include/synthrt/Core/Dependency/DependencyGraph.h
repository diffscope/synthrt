#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/srt_core_global.h>

namespace srt::dependency {

    struct SRT_CORE_EXPORT DependencyRequirement {
        std::string packageId;
        std::string moduleId;
        int level = -1;
        std::string versionRange = "*";

        bool operator==(const DependencyRequirement &other) const;
        std::string key() const;
    };

    struct SRT_CORE_EXPORT ResolvedDependency {
        std::string packageId;
        std::string moduleId;
        int level = -1;
        std::string version;

        bool operator==(const ResolvedDependency &other) const;
        std::string key() const;
    };

    struct SRT_CORE_EXPORT ModuleMetadata {
        std::string context;
        stdc::VersionNumber contextVersion;
        std::string packageId;
        std::string moduleId;
        std::string type;
        std::string iid;
        std::string configuration;
        std::filesystem::path packagePath;
        std::string version;
        int level = 0;
        std::vector<DependencyRequirement> requirements;
        std::vector<ResolvedDependency> resolvedDependencies;

        bool isSameMainModule(const ModuleMetadata &other) const;
        bool operator==(const ModuleMetadata &other) const;

        std::string key() const;
        std::string uniqueKey() const;

        struct MainModuleHash {
            size_t operator()(const ModuleMetadata &info) const;
        };

        struct MainModuleEqual {
            bool operator()(const ModuleMetadata &a, const ModuleMetadata &b) const;
        };
    };

    struct SRT_CORE_EXPORT VersionSelectionResult {
        bool success = false;
        std::string selectedVersion;
        std::string error;
        std::vector<std::string> candidates;
        std::vector<std::string> filteredCandidates;
    };

    struct SRT_CORE_EXPORT PackageInitializationPlan {
        std::string packageId;
        std::filesystem::path packagePath;
        std::vector<ModuleMetadata> initializationOrder; // 包含该包的所有模块，按初始化顺序排列
    };

    class SRT_CORE_EXPORT DependencyGraph {
    public:
        DependencyGraph();
        ~DependencyGraph();

        DependencyGraph(const DependencyGraph &) = delete;
        DependencyGraph &operator=(const DependencyGraph &) = delete;
        DependencyGraph(DependencyGraph &&) noexcept;
        DependencyGraph &operator=(DependencyGraph &&) noexcept;

        void addModule(const ModuleMetadata &module);
        bool buildGraph();
        void clear();

        std::vector<std::vector<ModuleMetadata>> findCycles() const;
        std::vector<ModuleMetadata> getAllModules() const;
        std::vector<PackageInitializationPlan> getPackageInitializationOrder() const;

    private:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

} // namespace srt::dependency
