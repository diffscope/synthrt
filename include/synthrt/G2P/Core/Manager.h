#pragma once

#include <string>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/Core/PackageManager.h>
#include <synthrt/G2P/Support/Error.h>
#include <synthrt/G2P/Task/Task.h>
#include <synthrt/G2P/srt_g2p_global.h>

namespace srt::g2p {

    /// Migration notice: this singleton Manager is a legacy migration surface.
    /// Do not add new feature work to this API. The target implementation is a
    /// runtime-scoped G2pService owned by srt::core::Runtime.
    ///
    /// Manager - top-level G2P manager, extends PackageManager with
    /// initialization and task lookup.
    ///
    /// Migrated from LangCore::Manager. Key invariants:
    ///   - D11: initialize() is hard idempotent — a second call returns
    ///     Error(AlreadyInitialized) instead of silently succeeding.
    ///   - D14: task() / tasks() use exact ContextKey match (no version
    ///     fallback); callers must supply the exact context+version.
    ///   - Singleton via instance(); not thread-safe to construct concurrently.
    ///
    /// Phase 3.1 scope: API surface complete; initialize() enforces idempotency
    /// but does not yet load packages or create tasks (full flow in P3.2+).
    class SRT_G2P_EXPORT Manager : public PackageManager {
    public:
        Manager();
        ~Manager() override;

        /// Singleton accessor.
        static Manager *instance();

        /// Initialize all registered contexts.
        ///
        /// Hard idempotent (D11): returns Error(AlreadyInitialized) if called
        /// more than once. This prevents state inconsistency from re-running
        /// the initialization flow.
        ///
        /// Phase 3.1: stubbed — marks `initialized = true` and returns success
        /// without loading packages or creating tasks (full flow in P3.2+).
        srt::core::Expected<void> initialize();

        bool initialized() const;

        /// Look up a single task by (category, context, version, id).
        /// D14: exact ContextKey match, no version fallback.
        srt::core::Expected<srt::core::NO<Task>> task(
            const std::string &category, const std::string &context,
            const std::string &id) const;
        srt::core::Expected<srt::core::NO<Task>> task(
            const std::string &category, const std::string &context,
            const stdc::VersionNumber &version, const std::string &id) const;

        /// List tasks for a (category, context, version).
        /// D14: exact ContextKey match, no version fallback.
        srt::core::Expected<std::vector<srt::core::NO<Task>>> tasks(
            const std::string &category, const std::string &context) const;
        srt::core::Expected<std::vector<srt::core::NO<Task>>> tasks(
            const std::string &category, const std::string &context,
            const stdc::VersionNumber &version) const;

        /// Run G2P conversion on a batch of inputs.
        /// Returns one G2pRes per input (preserves order).
        std::vector<G2pRes> convert(const std::vector<G2pInput> &input);

    private:
        srt::core::Expected<void> loadTasksForCategory(const std::string &category);

    protected:
        friend class Package;
        friend class srt::core::ModuleCategory;
    };

} // namespace srt::g2p
