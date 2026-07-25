#pragma once

#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <shared_mutex>
#include <vector>

#include <stdcorelib/3rdparty/llvm/smallvector.h>

#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Plugin/PluginFactory.h>

namespace srt::core {

    class ModuleCategory;

    /// Runtime::Impl - Standalone runtime state (ARCH-03).
    ///
    /// v4: no longer layers on PluginFactory::Impl. Runtime owns the composed
    /// PluginFactory and ObjectPool members directly; the ServiceRegistry holds
    /// the registered services (PluginFactory is registered as the
    /// PluginService). Runtime methods reach the runtime state through this
    /// single Impl via Runtime::_impl.
    class Runtime::Impl {
    public:
        explicit Impl(Runtime *q);
        ~Impl();

        Runtime *m_q;

        // --- Composed services (ARCH-03: composition over inheritance) ---
        // The ServiceRegistry owns the PluginFactory; `m_plugins` is a cached
        // non-owning pointer (== the instance registered in `m_services`) used
        // for fast forwarding.
        ServiceRegistry m_services;
        PluginFactory *m_plugins = nullptr;
        std::unique_ptr<ObjectPool> m_objectPool;

        // --- Stage 1 state (scanPackages) ---
        // Root directory passed to the last scanPackages() call.
        std::filesystem::path m_scanRoot;
        // Whether scanPackages() has ever been invoked successfully.
        bool m_scanned = false;
        // Discovered package directories under scanRoot (filesystem scan only,
        // no DLL loading).
        std::vector<std::filesystem::path> m_discoveredPackages;

        // --- Loaded package tracking (loadPackage / unloadPackage) ---
        // Records each successfully loaded package's canonical path and pkgId
        // so that unloadPackage(path) can resolve the pkgId without re-parsing
        // desc.json (which may have been modified on disk in the meantime).
        // Path comparison uses the canonical form to tolerate trailing slashes
        // and relative inputs.
        struct LoadedPackage {
            std::filesystem::path canonicalPath;
            std::string packageId;
        };
        std::vector<LoadedPackage> m_loadedPackages;

        // --- Stage 2 state (initialize) ---
        // initialize() is one-shot hard-idempotent. `m_initializing` guards
        // against concurrent entry; `m_initialized` records terminal success.
        std::atomic<bool> m_initializing{false};
        std::atomic<bool> m_initialized{false};

        // --- Module system state ---
        // Categories registered through ModuleCategoryRegistrar, keyed by name
        // (e.g. "inference", "singer") and by manifest key (e.g. "inferences").
        std::map<std::string, ModuleCategory *, std::less<>> m_moduleCategories;
        std::map<std::string, ModuleCategory *, std::less<>> m_moduleCateKeyMap;

        // Shared mutex protecting category/contribute operations across packages.
        mutable std::shared_mutex m_su_mtx;

    public:
        static llvm::SmallVector<ModuleCategory *(*)(Runtime *)> moduleCategoryFactories;
    };

}
