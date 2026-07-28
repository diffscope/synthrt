#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include <stdcorelib/adt/array_view.h>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Core/ServiceRegistry.h>
#include <synthrt/Core/Support/Expected.h>

namespace srt::core {

    class ModuleCategory;
    template <class T>
    class ModuleCategoryRegistrar;

    /// Runtime - The runtime state entry point. Owns a ServiceRegistry and
    /// composes (rather than inherits) a PluginFactory (plugin discovery/load)
    /// and an ObjectPool (named object registry).
    ///
    /// v4 (ARCH-03): Runtime no longer multiply inherits PluginFactory +
    /// ObjectPool. Callers acquire composed services through services().
    ///
    /// Runtime implements the two-phase initialization defined in the
    /// refactoring design:
    ///   - Stage 1: \c scanPackages() - filesystem + JSON parsing only, no DLL
    ///              loading. May be called repeatedly before \c initialize().
    ///   - Stage 2: \c initialize()   - one-shot, hard-idempotent. Loads
    ///              models, registers drivers/interpreters and prepares the
    ///              runtime for inference.
    class SRT_CORE_EXPORT Runtime {
    public:
        Runtime();
        ~Runtime();

    public:
        // --- Service registry (ARCH-03) ---
        // Runtime owns a ServiceRegistry; services are registered/looked up by
        // type. The PluginFactory is registered here as the PluginService.
        ServiceRegistry &services();
        const ServiceRegistry &services() const;

        // --- Stage 1: package scanning ---
        /// Scan a package root directory. Only performs filesystem + metadata
        /// inspection; does not load shared libraries. May be called repeatedly
        /// before \c initialize(). Runtime package sources are immutable after
        /// initialization.
        Expected<void> scanPackages(const std::filesystem::path &rootDir);

        // --- Stage 2: initialization ---
        /// Full initialization. One-shot and hard-idempotent — once it has
        /// completed (success or failure) it must not be retried, mirroring the
        /// LangCore \c Manager::initialize() contract. Internally performs:
        ///   2.1 register ONNX driver
        ///   2.2 build context index from LanguageConfig
        ///   2.3 bind models to drivers
        ///   2.4 mark Ready
        ///
        /// \note The sub-steps depend on modules not yet migrated (Language,
        ///       ds-infer, ds-session); they are wired up in their respective
        ///       phases. Until then initialize() succeeds as a no-op Stage 2.
        Expected<void> initialize();

        /// Returns \c true once Stage 2 has completed successfully.
        bool isInitialized() const;

        /// Returns the package directories discovered during scanPackages()
        /// calls. Empty if scanPackages() has not been called or found nothing.
        std::vector<std::filesystem::path> discoveredPackages() const;

        // --- Module category registry ---
        ModuleCategory *moduleCategory(const std::string_view &name) const;

        /// Load a DiffSinger voice bank package at \c path. Parses
        /// \c desc.json and delegates to InferenceCategory/SingerCategory to
        /// create and load specs. Inference specs are loaded first so that
        /// singer \c loadSpec(Initialized) can resolve InferenceSpec pointers
        /// by inferenceId.
        ///
        /// Specs are transitioned through \c Initialized → \c Ready (models
        /// loaded). Replaces the former \c open(path, noLoad) which returned
        /// a \c PackageRef stub; package state is now tracked internally.
        Expected<void> loadPackage(const std::filesystem::path &path);

        /// Unload a DiffSinger voice bank package previously loaded via
        /// \c loadPackage(). The \c path argument must match the path passed
        /// to \c loadPackage() (path identity is checked against the internal
        /// loaded-package list).
        ///
        /// Internally this:
        ///   1. Looks up the package by \c path and retrieves its pkgId.
        ///   2. Iterates the inference and singer module categories, finds
        ///      every spec whose \c packageId() matches, and transitions it
        ///      through \c ModuleSpec::Deleted (removing it from the category
        ///      and freeing its memory).
        ///
        /// \warning The caller MUST ensure no active ModelSet handles
        ///          reference specs from this package before calling
        ///          unloadPackage. This API performs no active-reference
        ///          check itself; unloading a package that is still in use
        ///          results in undefined behavior (dangling spec references
        ///          in the upper layer). \c VoicebankSession::unloadVoicebank
        ///          provides reference counting (via \c activeHandleCount,
        ///          returning \c PackageInUse when handles are outstanding)
        ///          to enforce this; direct Runtime consumers must implement
        ///          their own guard. Per ROBUST-04, the unload order is:
        ///          unload models first, then unload the package.
        ///
        /// \note ARCH-03 layering: Runtime (srt::core) only unloads the
        ///       ModuleSpec entries it owns. Callers that maintain a
        ///       \c ds::infer::ModelRegistry MUST additionally invoke
        ///       \c ModelRegistry::unbindPackage(pkgId) to evict cached
        ///       InferenceSession objects. Callers that maintain a
        ///       \c ds::infer::ModelSet MUST additionally invoke
        ///       \c ModelSet::markStale() (or rebuild the ModelSet) to
        ///       invalidate handles that still reference the unloaded
        ///       package's specs (ROBUST-04). Failing to do so leaves
        ///       dangling references in the upper layer.
        ///
        /// \note The recommended unload order is: (1) mark ModelSet stale
        ///       and stop in-flight inference, (2) call this API to remove
        ///       specs, (3) call \c ModelRegistry::unbindPackage(pkgId).
        Expected<void> unloadPackage(const std::filesystem::path &path);

        // --- Destruction callbacks (DLL unload ordering) ---
        // Register a callback to be invoked in ~Runtime(), BEFORE PluginFactory
        // unloads plugin DLLs. This allows subsystems that hold shared_ptrs to
        // plugin-DLL-resident objects (e.g. srt::g2p::Manager holding Task/
        // SessionFactory instances whose vtables live in srt-driver-onnx.dll)
        // to release those references while the plugin DLLs are still loaded.
        //
        // The callback executes during Runtime destruction, which happens
        // BEFORE static singleton destructors run. Without this hook, a static
        // singleton (e.g. srt::g2p::Manager) would release its shared_ptrs in
        // its own destructor (after Runtime destruction), accessing freed
        // plugin DLL memory through dangling vtable pointers.
        //
        // Callbacks run in reverse order of registration (LIFO). The callback
        // MUST NOT capture a shared_ptr to the Runtime itself (would create a
        // cycle); capture raw pointers or weak_ptr instead.
        void addDestructionCallback(std::function<void()> callback);

        // --- Global shutdown hook (process-wide singleton cleanup) ---
        // Set a process-wide shutdown hook that will be called once in
        // ~Runtime::Impl() BEFORE plugin DLLs are unloaded. This is used by
        // subsystems that are static singletons (e.g. srt::g2p::PackageManager)
        // and cannot access a Runtime instance to call addDestructionCallback.
        //
        // The hook is cleared after execution, so only the first Runtime
        // destruction triggers it. If multiple Runtime instances exist, only
        // the first one's destruction runs the hook.
        //
        // The hook MUST NOT capture a shared_ptr to any Runtime instance.
        // Capture raw pointers to static singletons (whose lifetime exceeds
        // Runtime) or weak_ptr instead.
        static void setGlobalShutdownHook(std::function<void()> hook);

    protected:
        static void registerModuleCategoryFactory(ModuleCategory *(*fac)(Runtime *));

        friend class ModuleCategory;
        template <class T>
        friend class ModuleCategoryRegistrar;

    private:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

}
