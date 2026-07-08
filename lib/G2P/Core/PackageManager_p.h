#ifndef SRT_G2P_CORE_PACKAGEMANAGER_P_H
#define SRT_G2P_CORE_PACKAGEMANAGER_P_H

#include <list>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <stdcorelib/3rdparty/llvm/smallvector.h>
#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Plugin/PluginFactory.h>
#include <synthrt/Core/Support/ContextKey.h>

#include <synthrt/G2P/Core/PackageManager.h>

namespace srt::g2p {

    class PackageData;

    class PackageManager::Impl {
    public:
        explicit Impl(PackageManager *decl);
        virtual ~Impl();

        using Decl = PackageManager;
        PackageManager *decl;

        struct LoadedPackageBlock {
            PackageData *spec = nullptr;
            int ref = 0;
            llvm::SmallVector<srt::core::ModuleSpec *> contributes;
            llvm::SmallVector<PackageData *> linked;
        };

        struct PackageBrief {
            std::filesystem::path path;
            stdc::VersionNumber compatVersion;
        };

        struct LoadedPackageMap {
            std::list<LoadedPackageBlock> packages;
            std::map<std::filesystem::path::string_type, decltype(packages)::iterator, std::less<>> pathIndexes;
            std::map<std::string, std::unordered_map<stdc::VersionNumber, decltype(packages)::iterator>, std::less<>> idIndexes;
            std::unordered_map<PackageData *, decltype(packages)::iterator> pointerIndexes;
        };

        srt::core::Expected<PackageData *> openPackage(const std::filesystem::path &path);
        bool closePackage(PackageData *spec);
        void closeAllLoadedPackages();
        void refreshPackageIndexes(const srt::core::ContextKey &ctxKey);

        // Category registry
        std::map<std::string, srt::core::ModuleCategory *, std::less<>> categories;
        std::map<std::string, srt::core::ModuleCategory *, std::less<>> cateKeyMap;

        // Per-context package paths
        std::map<srt::core::ContextKey, llvm::SmallVector<std::filesystem::path>> contextPackagePaths;

        // Per-context initialization states
        std::map<srt::core::ContextKey, ContextState> contextStates;

        // C-8: per-context dependency errors
        std::map<srt::core::ContextKey, std::vector<std::string>> contextDependencyErrors;

        // Per-context module metadata cache
        std::map<srt::core::ContextKey, std::vector<srt::dependency::ModuleMetadata>> contextModuleInfos;
        std::map<srt::core::ContextKey, bool> contextDependencyResolved;

        // Per-context dependency graphs
        std::map<srt::core::ContextKey, srt::dependency::DependencyGraph> contextDependencyGraphs;

        // Loaded package management
        LoadedPackageMap loadedPackageMap;
        std::unordered_set<PackageData *> resourcePackages;
        bool packagePathsDirty = false;
        std::map<srt::core::ContextKey, std::map<std::string, std::map<stdc::VersionNumber, PackageBrief>, std::less<>>> contextCachedIndexes;
        std::map<std::string, std::unordered_map<stdc::VersionNumber, std::filesystem::path>, std::less<>> pendingPackages;

        // 3-level map: category → ContextKey → moduleId → Task
        std::map<std::string, std::map<srt::core::ContextKey, std::map<std::string, srt::core::NO<Task>>>> tasks;

        mutable std::shared_mutex tasks_mtx;

        bool initialized = false;

        static constexpr int kCurrentLevel = 2;
        static constexpr int kMaximumLevel = 2;
        static constexpr int kMinimumLevel = 1;

        mutable std::shared_mutex su_mtx;
    };

} // namespace srt::g2p

#endif // SRT_G2P_CORE_PACKAGEMANAGER_P_H
