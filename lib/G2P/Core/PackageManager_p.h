#pragma once

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
        explicit Impl(PackageManager *q);
        virtual ~Impl();

        PackageManager *m_q;

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

        /// Erase all per-context state for \p ctxKey (TD-02). Centralizes the
        /// 7-map erase previously duplicated in removeContextsByPrefix
        /// overloads. New per-context state should be added here so retire
        /// paths do not silently leak entries (D-24 / ROBUST-05).
        void eraseContextState(const srt::core::ContextKey &ctxKey);

        // Category registry
        std::map<std::string, srt::core::ModuleCategory *, std::less<>> m_categories;
        std::map<std::string, srt::core::ModuleCategory *, std::less<>> m_cateKeyMap;

        // Per-context package paths
        std::map<srt::core::ContextKey, llvm::SmallVector<std::filesystem::path>> m_contextPackagePaths;

        // Per-context initialization states
        std::map<srt::core::ContextKey, ContextState> m_contextStates;

        // C-8: per-context dependency errors
        std::map<srt::core::ContextKey, std::vector<std::string>> m_contextDependencyErrors;

        // Per-context module metadata cache
        std::map<srt::core::ContextKey, std::vector<srt::dependency::ModuleMetadata>> m_contextModuleInfos;
        std::map<srt::core::ContextKey, bool> m_contextDependencyResolved;

        // Per-context dependency graphs
        std::map<srt::core::ContextKey, srt::dependency::DependencyGraph> m_contextDependencyGraphs;

        // Loaded package management
        LoadedPackageMap m_loadedPackageMap;
        std::unordered_set<PackageData *> m_resourcePackages;
        bool m_packagePathsDirty = false;
        std::map<srt::core::ContextKey, std::map<std::string, std::map<stdc::VersionNumber, PackageBrief>, std::less<>>> m_contextCachedIndexes;
        std::map<std::string, std::unordered_map<stdc::VersionNumber, std::filesystem::path>, std::less<>> m_pendingPackages;

        // 3-level map: category → ContextKey → moduleId → Task
        std::map<std::string, std::map<srt::core::ContextKey, std::map<std::string, srt::core::NO<Task>>>> m_tasks;

        mutable std::shared_mutex m_tasks_mtx;

        bool m_initialized = false;

        static constexpr int kCurrentLevel = 2;
        static constexpr int kMaximumLevel = 2;
        static constexpr int kMinimumLevel = 1;

        mutable std::shared_mutex m_su_mtx;
    };

} // namespace srt::g2p
