#pragma once

#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/Base/LangCommon.h>
#include <diffsinger/Bank/PackageManifest.h>
#include <diffsinger/Bank/PackageStatus.h>
#include <diffsinger/Bank/SingerRef.h>
#include <diffsinger/Bank/SingerSnapshot.h>
#include <diffsinger/Session/dssession_global.h>

namespace srt::g2p {
    class LanguageService;
}

namespace srt::core {
    class Runtime;
}

namespace ds::session {

    class ModelSetHandle;

    enum class AvailabilityLevel { Available, Degraded, Unavailable };

    struct AvailabilitySummary {
        size_t available = 0;
        size_t degraded = 0;
        size_t unavailable = 0;
    };

    struct DSSESSION_EXPORT VoicebankSnapshot {
        std::vector<ds::bank::SingerSnapshot> singers;
        std::vector<ds::bank::PackageStatus> packages;
        /// TD-01 (D-39 #2): full manifests for valid packages, ordered by
        /// discovery (same order as `packages` filtered to valid entries).
        /// Lets lite PackageManager read author/description/license/singer/
        /// speaker/language detail directly from the snapshot without keeping
        /// a separate PackageCatalog. Invalid packages contribute only their
        /// PackageStatus.error; they have no manifest here.
        std::vector<ds::bank::PackageManifest> manifests;
        std::vector<std::filesystem::path> roots;
        std::vector<std::string> reservedPhonemes;
        AvailabilitySummary availability;
        unsigned long long generation = 0;

        // === V3-07 fingerprint (D-33) ===
        std::string catalogFingerprint;    // Stable digest of package catalog content
        std::string languageFingerprint;   // Stable digest of language route content

        // === A2: const query methods (lite integration) ===
        // Lets hosts query singer/package/manifest without re-implementing the
        // O(n) traversal. The snapshot is immutable after publication, so these
        // const methods are safe to call concurrently (A2-T11). All pointers
        // returned are non-owning and valid only while the snapshot is alive.

        /// Find a singer snapshot by exact (packageId, singerId, version) match.
        /// Returns nullptr if not found. Version comparison uses
        /// stdc::VersionNumber::fromString().value_or(stdc::VersionNumber()) normalization so "1.0" matches
        /// "1.0.0"; empty version matches only empty version (A2-T02).
        const ds::bank::SingerSnapshot *findSinger(const ds::bank::SingerRef &ref) const;

        /// Find singers by singerId alone (may return multiple for multi-version
        /// same-packageId scenarios). Returned vector is non-owning; valid only
        /// while the snapshot is alive.
        std::vector<const ds::bank::SingerSnapshot *>
            findSingersBySingerId(const std::string &singerId) const;

        /// Find a package status by (packageId, version) match. Returns nullptr
        /// if not found. Invalid packages remain in `packages` with valid=false
        /// and are returned by this method too (callers check the `valid` field).
        const ds::bank::PackageStatus *
            findPackage(const std::string &packageId,
                        const stdc::VersionNumber &version) const;

        /// Find a package manifest by (packageId, version) match. Returns
        /// nullptr if not found or the package is invalid (invalid packages
        /// have no manifest entry — TD-01).
        const ds::bank::PackageManifest *
            findManifest(const std::string &packageId,
                         const stdc::VersionNumber &version) const;
    };

    /// Resources borrowed by VoicebankSession (V3-06). Lifetime is owned by the
    /// host (e.g. ds-editor-lite's SynthrtEngine). Session only borrows via
    /// references; it does not extend lifetime. Host must outlive the session.
    struct DSSESSION_EXPORT SessionResources {
        srt::core::Runtime *runtime = nullptr;                        // Required for createModelSet
        std::shared_ptr<srt::g2p::LanguageService> languageService;   // Required for convertG2p/convertS2p
        /// G2P plugin search paths for LanguageService initialization.
        /// Passed to initializeMetadata() / updateMetadata() during refresh().
        /// v7 erratum (docs/modules/ds-session.md §21): the auto-initialization
        /// trigger is `languageService != nullptr` (`if (svc)`), NOT
        /// `g2pPluginPaths` non-empty. Even when empty, refresh() still calls
        /// initializeMetadata() as long as a LanguageService is injected.
        std::vector<std::filesystem::path> g2pPluginPaths;
        /// Official G2P package paths (e.g. resources/G2pPackages/).
        /// Passed alongside g2pPluginPaths to initializeMetadata().
        std::vector<std::filesystem::path> officialG2pPackages;
    };

    /// PackageCoordinate - Stable coordinate identifying one installed package.
    /// Used by ChangeSummary and updatesAvailable to report per-package deltas.
    struct DSSESSION_EXPORT PackageCoordinate {
        std::string packageId;
        stdc::VersionNumber version;

        bool operator==(const PackageCoordinate &) const = default;
    };

    /// ChangeSummary - Per-package delta produced by refresh().
    /// Each list holds PackageCoordinate entries that were added/removed/changed
    /// or became disabled relative to the previous snapshot.
    struct DSSESSION_EXPORT ChangeSummary {
        std::vector<PackageCoordinate> added;
        std::vector<PackageCoordinate> removed;
        /// Installed packages whose coordinate stayed the same but whose
        /// observable metadata or singer capabilities changed.
        std::vector<PackageCoordinate> changed;
        std::vector<PackageCoordinate> disabled;
    };

    /// RefreshResult - Result of a refresh() call. On success carries the new
    /// snapshot plus a change summary and diagnostics; on failure carries the
    /// previous snapshot (unchanged) and an error message.
    struct DSSESSION_EXPORT RefreshResult {
        bool succeeded = false;
        bool coalesced = false;
        bool changed = false;                              ///< Whether the new snapshot differs from the previous one
        bool languageReady = false;                        ///< Whether G2P/S2P language module is ready after refresh
        std::shared_ptr<const VoicebankSnapshot> snapshot;
        ChangeSummary changes;                             ///< Per-package delta (added/removed/changed/disabled)
        std::vector<srt::core::Diagnostic> diagnostics;    ///< Diagnostics collected during refresh
        /// Same-coordinate updates. This mirrors changed so hosts can offer an
        /// explicit reload/migration action without treating it as a version
        /// change.
        std::vector<PackageCoordinate> updatesAvailable;
        std::string errorMessage;                          ///< Short message on failure
    };

    /// Availability - Per-singer availability verdict consumed by Lite.
    /// Ready: full inference chain can be built.
    /// Warning: usable but with capability reduction or unprovable items.
    /// Disabled: cannot infer (missing required stage/resource/G2P).
    enum class Availability { Ready, Warning, Disabled };

    /// SingerCapabilitySummary - Lite-facing summary of a singer's capability.
    /// Maps the internal SingerCapabilityReport to the three-state Availability
    /// plus the language/phoneme/speaker lists Lite needs for UI display.
    struct DSSESSION_EXPORT SingerCapabilitySummary {
        Availability availability = Availability::Disabled;
        std::vector<std::string> languages;
        std::vector<std::string> phonemes;
        std::vector<std::string> mixableSpeakers;
        std::vector<srt::core::Diagnostic> diagnostics;
    };

    /// S2pResult - Output of session-level S2P conversion.
    /// phonemes and onsets are parallel arrays: onsets[i] is true when
    /// phonemes[i] belongs to the onset of syllable i.
    struct DSSESSION_EXPORT S2pResult {
        std::vector<std::string> phonemes;
        std::vector<bool> onsets;
    };

    /// RAII handle for a refresh completion subscription. Destroying or resetting
    /// the handle prevents any later notification. It does not cancel refreshes.
    class DSSESSION_EXPORT RefreshSubscription {
    public:
        RefreshSubscription() = default;
        ~RefreshSubscription();
        RefreshSubscription(const RefreshSubscription &) = delete;
        RefreshSubscription &operator=(const RefreshSubscription &) = delete;
        RefreshSubscription(RefreshSubscription &&other) noexcept;
        RefreshSubscription &operator=(RefreshSubscription &&other) noexcept;

        /// Stop receiving future refresh completion notifications. Safe to call
        /// from the callback itself.
        void reset();
        explicit operator bool() const noexcept;

        class State;

    private:
        explicit RefreshSubscription(std::shared_ptr<State> state);

        std::shared_ptr<State> _state;

        friend class VoicebankSession;
    };

    /// S2: Result of loadVoicebank.
    enum class LoadResult { NewlyLoaded, AlreadyLoaded };

    /// S2: Info about a loaded voicebank package.
    struct DSSESSION_EXPORT LoadedVoicebankInfo {
        std::string packageId;
        stdc::VersionNumber version;
        std::filesystem::path packagePath;
        unsigned long long loadedGeneration = 0;
        int activeHandleCount = 0;
        bool pendingUnload = false;
    };

    /// S2: Tag type for force-unload overload.
    struct DSSESSION_EXPORT ForceUnloadTag {};

    /// Thread-safe owner of an atomically published, immutable voicebank view.
    class DSSESSION_EXPORT VoicebankSession {
    public:
        VoicebankSession();
        /// Resource-injected constructor: full functionality. Host owns Runtime
        /// and LanguageService; session borrows them. Must not be called with
        /// null runtime / null languageService (use default constructor for
        /// discovery-only scenarios).
        explicit VoicebankSession(SessionResources resources);
        ~VoicebankSession();
        VoicebankSession(const VoicebankSession &) = delete;
        VoicebankSession &operator=(const VoicebankSession &) = delete;
        // Move-only: lets hosts reassign m_session after pluginRoot() becomes
        // available (B1a in ds-editor-lite). Impl is shared_ptr-backed, so the
        // moved-from session leaves the Impl empty and is safe to destruct.
        VoicebankSession(VoicebankSession &&) noexcept;
        VoicebankSession &operator=(VoicebankSession &&) noexcept;

        void setRoots(std::vector<std::filesystem::path> roots);
        std::vector<std::filesystem::path> roots() const;
        void setReservedPhonemes(std::vector<std::string> phonemes);
        std::vector<std::string> reservedPhonemes() const;

        /// Inject the LanguageService used by convertG2p/convertS2p. The
        /// session does not own initialization lifecycle of the service; the
        /// caller must initialize() it before invoking conversions. Passing
        /// nullptr disables G2P/S2P (subsequent convert calls return
        /// ErrorCode::G2pNotImplementedError).
        std::shared_ptr<srt::g2p::LanguageService> languageService() const;

        /// Refresh and return the final result. Concurrent calls share one scan.
        /// This is the preferred API for CLI, tests, and hosts that already run
        /// package discovery on a worker thread.
        RefreshResult refresh();

        /// Start one background scan. Concurrent callers share the in-flight
        /// operation and its result. Use this only when the host needs to own
        /// scheduling rather than calling refresh() from its worker.
        std::shared_future<RefreshResult> refreshAsync();

        /// Subscribe to refresh publications and final failures. The callback is
        /// called once for each refresh that publishes changed content or fails;
        /// concurrent refreshAsync callers share a refresh and therefore one
        /// notification. Callbacks run outside internal locks and may reset
        /// their own handle.
        RefreshSubscription subscribeRefresh(std::function<void(const RefreshResult &)> callback);

        std::shared_ptr<const VoicebankSnapshot> snapshot() const;
        AvailabilitySummary availability() const;

        /// Per-singer capability summary derived from the current snapshot's
        /// SingerCapabilityReport. Unknown singers are Disabled. A resolved
        /// legacy singer without a report remains Ready, but phoneme validation
        /// will reject requests because support cannot be proven.
        SingerCapabilitySummary capabilitySummary(const ds::bank::SingerRef &singerKey) const;

        /// G2P conversion via the injected LanguageService. Does not copy
        /// fallback and does not write empty phonemes; on failure returns an
        /// Expected error that Lite must surface (vnext 04 migration rules).
        srt::core::Expected<std::vector<srt::g2p::G2pRes>>
            convertG2p(const ds::bank::SingerRef &singerKey,
                       const std::string &language,
                       const std::vector<srt::g2p::G2pInput> &inputs) const;

        /// S2P conversion: resolves the per-singer S2P resource via the
        /// injected LanguageService and runs LanguageResource::convert().
        srt::core::Expected<S2pResult>
            convertS2p(const ds::bank::SingerRef &singerKey,
                       const std::string &language,
                       const std::string &pronunciation) const;

        /// Final phoneme validation against the singer's effective phonemes.
        /// Missing or unsupported phonemes return an Expected error and must
        /// block downstream inference (no silent replacement).
        srt::core::Expected<void>
            validatePhonemes(const ds::bank::SingerRef &singerKey,
                             const std::vector<std::string> &phonemes) const;

        /// Inject the Runtime used by createModelSet to resolve inference
        /// stages. The session does not own the Runtime lifecycle; the caller
        /// must loadPackage() the singer's package before calling
        /// createModelSet(). Passing nullptr disables ModelSet
        /// creation (subsequent createModelSet returns
        /// ErrorCode::InferenceNotInitialized).
        srt::core::Runtime *runtime() const;

        /// Create a ModelSetHandle bound to the current snapshot generation.
        /// singerKey must exist in the current snapshot and be Resolved. The
        /// Runtime must be set and have the singer's package loaded. After a
        /// successful refresh() that publishes changed content, the returned
        /// handle's start() returns ErrorCode::StaleModelSet (running tasks
        /// may still finish; load/stop/unload remain usable).
        srt::core::Expected<std::shared_ptr<ModelSetHandle>>
            createModelSet(const ds::bank::SingerRef &singerKey);

        /// Ensure the language module (G2P + S2P) for a (packageId, version,
        /// language) is loaded. Sync, blocking. Idempotent. Host must call this
        /// before convertG2p/convertS2p on a fresh session or after a refresh
        /// that removed the language module (V3-08).
        srt::core::Expected<void> ensureLanguageReady(
            const std::string &packageId,
            const stdc::VersionNumber &version,
            const std::string &language);

        /// Ensure the ModelSet for a singer is loaded and return a handle bound
        /// to the current snapshot generation. Thin wrapper over createModelSet
        /// with explicit error categorization (V3-08).
        srt::core::Expected<std::shared_ptr<ModelSetHandle>>
            ensureModelSet(const ds::bank::SingerRef &singerKey);

        // === S2: Explicit package load/unload API ===

        /// Load a voicebank package into Runtime. Idempotent: returns
        /// AlreadyLoaded if the (packageId, version) is already loaded.
        /// The package must exist in the current snapshot.
        srt::core::Expected<LoadResult> loadVoicebank(
            const std::string &packageId,
            const stdc::VersionNumber &version);

        /// Unload a voicebank package, freeing Runtime resources.
        /// Returns PackageInUse if active ModelSetHandle references exist
        /// (sets pendingUnload=true for deferred unload).
        srt::core::Expected<void> unloadVoicebank(
            const std::string &packageId,
            const stdc::VersionNumber &version);

        /// Force unload: marks all handles stale, then unloads immediately.
        /// Intended for shutdown scenarios.
        srt::core::Expected<void> unloadVoicebank(
            const std::string &packageId,
            const stdc::VersionNumber &version,
            ForceUnloadTag);

        /// Query currently loaded voicebanks with reference counts.
        std::vector<LoadedVoicebankInfo> loadedVoicebanks() const;

    private:
        class Impl;
        std::shared_ptr<Impl> _impl;
    };

} // namespace ds::session
