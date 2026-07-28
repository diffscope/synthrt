#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Dependency/DependencyGraph.h>
#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Plugin/PluginFactory.h>
#include <synthrt/Core/Support/ContextKey.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/JSON.h>
#include <synthrt/G2P/Package/Package.h>
#include <synthrt/G2P/Support/Error.h>
#include <synthrt/G2P/srt_g2p_global.h>

namespace srt::g2p {

    class Task;

    /// ContextState - framework context lifecycle state (4-state design, D10).
    ///
    /// Distinct from ds::bank::SingerSnapshot.resolutionState (3-state, host-side):
    ///   - SingerSnapshot expresses voice bank runtime resolution state (bank side)
    ///   - ContextState expresses framework context lifecycle (framework side)
    enum class ContextState {
        Pending,       ///< Registered but not yet initialized
        Ready,         ///< Initialization succeeded (at least one package loaded)
        Failed,        ///< Initialization failed (missing deps / cycle / level
                       ///< incompatible / driver unavailable; does not block
                       ///< other contexts)
        NotRegistered, ///< Not registered (not enumerated in contextKeys())
    };

    /// PackageManager - manages G2P package loading and module registration.
    ///
    /// Migrated from LangCore::PackageManager. Extends srt::core::PluginFactory
    /// to inherit plugin loading (filesystem/runtime/static). Owns
    /// ModuleCategory instances (registered via ModuleCategoryRegistrar) and
    /// loaded PackageData instances.
    ///
    /// Phase 3.1 scope: infrastructure migration — API surface is complete but
    /// complex operations (package open, dependency resolution, module task
    /// creation) are stubbed returning NotImplementedError. Full implementation
    /// lands in P3.2+.
    class SRT_G2P_EXPORT PackageManager : public srt::core::PluginFactory {
    public:
        PackageManager();
        ~PackageManager() override;

        /// Returns merged dependency errors across all contexts (backward compat).
        std::vector<std::string> getDependencyErrors() const;
        /// C-8: per-context dependency errors (ARCH-01/ROBUST-03).
        std::vector<std::string> getDependencyErrors(const srt::core::ContextKey &ctxKey) const;

        srt::core::ModuleCategory *category(const std::string_view &name) const;

        /// Register a package path under (context, version). Per R-8:
        ///   - empty context (default) requires empty version;
        ///   - non-empty context requires non-empty version.
        srt::core::Expected<void> addPackagePath(const std::string &context,
                                                  const stdc::VersionNumber &version,
                                                  const std::filesystem::path &path);
        /// Replace all package paths for (context, version). Same R-8 rules.
        srt::core::Expected<void> setPackagePaths(const std::string &context,
                                                   const stdc::VersionNumber &version,
                                                   const std::vector<std::filesystem::path> &paths);
        /// Query package paths registered under (context, version).
        std::vector<std::filesystem::path> packagePaths(const std::string &context,
                                                         const stdc::VersionNumber &version) const;
        /// Enumerate all registered (context, version) keys.
        std::vector<srt::core::ContextKey> contextKeys() const;

        /// Remove contexts whose name starts with \p prefix AND whose version
        /// matches \p version exactly (V3-01 §2.4 / D-43). Required for
        /// multi-version same-packageId hot reload: removing one version of a
        /// packageId must not retire contexts belonging to other versions of
        /// the same packageId (D-24 multi-version coexistence, ROBUST-05 no
        /// implicit error swallowing).
        ///
        /// An empty \p version matches only unversioned contexts (default
        /// context). This is rarely the intended behavior for hot reload;
        /// callers should pass the concrete version being retired.
        ///
        /// Used by LanguageService::updateMetadata to remove retired voicebank
        /// G2P contexts before re-registering. Only removes contexts whose
        /// state is Pending (i.e. Manager::initialize() has not been called);
        /// after initialize() contexts are immutable and this function returns
        /// an error for any non-Pending matching context.
        ///
        /// Returns the number of contexts actually removed. Errors:
        ///   - G2pAlreadyInitialized: Manager::initialize() was already called;
        ///     caller must restart the host process.
        srt::core::Expected<size_t> removeContextsByPrefix(
            const std::string &prefix, const stdc::VersionNumber &version);

        /// Query initialization state for a context.
        /// Unregistered contexts (not in contextKeys()) return NotRegistered.
        ContextState contextState(const srt::core::ContextKey &ctxKey) const;

        /// List all contexts whose initialization failed (excludes the default
        /// context — default-context failure blocks initialize()).
        std::vector<srt::core::ContextKey> failedContexts() const;

        /// Open and parse a single package (parse package.json + module specs),
        /// registering it in the loaded package map. Does NOT resolve transitive
        /// dependencies — that is done by addPackagePath + Manager::initialize().
        srt::core::Expected<Package> open(const std::filesystem::path &path);
        Package find(const std::string_view &id, const stdc::VersionNumber &version) const;
        std::vector<Package> find(const std::string_view &id) const;
        std::vector<Package> packages() const;

        srt::core::Expected<srt::core::NO<Task>> createModuleTask(
            const srt::dependency::ModuleMetadata &moduleInfo, const Package &pkg) const;

        std::vector<srt::dependency::ModuleMetadata> getModuleMetadatas(const std::string &context);
        std::vector<srt::dependency::ModuleMetadata> getModuleMetadatas(
            const srt::core::ContextKey &ctxKey);

        /// Release all object references held by category ObjectPools and
        /// clear loaded packages. Called automatically by setupG2pOnnxDriver's
        /// Runtime destruction callback, so manual invocation is typically
        /// unnecessary.
        ///
        /// This is the counterpart to the constructor: it performs the same
        /// cleanup as the destructor but can be called early (before plugin
        /// DLLs are unloaded) to prevent use-after-free when static singleton
        /// destructors run after Runtime destruction.
        void shutdown();

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;
        explicit PackageManager(Impl &impl);

        friend class Package;
        friend class PackageData;
        friend class srt::core::ModuleCategory;
        friend class srt::core::ModuleSpec;
    };

} // namespace srt::g2p
