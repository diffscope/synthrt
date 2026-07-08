#pragma once

#include <filesystem>
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
