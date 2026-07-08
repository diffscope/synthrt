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
        explicit Impl(Runtime *decl);
        ~Impl();

        using Decl = Runtime;
        Runtime *_decl;

        // --- Composed services (ARCH-03: composition over inheritance) ---
        // The ServiceRegistry owns the PluginFactory; `plugins` is a cached
        // non-owning pointer (== the instance registered in `services`) used
        // for fast forwarding.
        ServiceRegistry services;
        PluginFactory *plugins = nullptr;
        std::unique_ptr<ObjectPool> objectPool;

        // --- Stage 1 state (scanPackages) ---
        // Root directory passed to the last scanPackages() call.
        std::filesystem::path scanRoot;
        // Whether scanPackages() has ever been invoked successfully.
        bool scanned = false;
        // Discovered package directories under scanRoot (filesystem scan only,
        // no DLL loading).
        std::vector<std::filesystem::path> discoveredPackages;

        // --- Stage 2 state (initialize) ---
        // initialize() is one-shot hard-idempotent. `initializing` guards
        // against concurrent entry; `initialized` records terminal success.
        std::atomic<bool> initializing{false};
        std::atomic<bool> initialized{false};

        // --- Module system state ---
        // Categories registered through ModuleCategoryRegistrar, keyed by name
        // (e.g. "inference", "singer") and by manifest key (e.g. "inferences").
        std::map<std::string, ModuleCategory *, std::less<>> moduleCategories;
        std::map<std::string, ModuleCategory *, std::less<>> moduleCateKeyMap;

        // Shared mutex protecting category/contribute operations across packages.
        mutable std::shared_mutex su_mtx;

    public:
        static llvm::SmallVector<ModuleCategory *(*)(Runtime *)> moduleCategoryFactories;
    };

}
