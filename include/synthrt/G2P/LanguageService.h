#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/LanguageRoute.h>
#include <synthrt/G2P/srt_g2p_global.h>
#include <synthrt/S2P/LanguageResource.h>

namespace srt::g2p {

    /// One package directory entry passed to LanguageService::initializeMetadata().
    /// Replaces the legacy unordered_map<packageId, path> which lost version info:
    /// multi-version same-packageId voicebanks can now coexist across all 5
    /// layers (V3-01). Callers (e.g. VoicebankSession) build this vector from
    /// VoicebankScanner::packageDirectories().
    struct SRT_G2P_EXPORT PackageDirectoryEntry {
        std::string packageId;
        stdc::VersionNumber version;
        std::filesystem::path path;

        bool operator==(const PackageDirectoryEntry &) const = default;
    };

    /// Diff produced by LanguageService::updateMetadata() (V3-16 hot reload).
    /// Lists voicebank packages that were added, removed, or unchanged relative
    /// to the previous initializeMetadata()/updateMetadata() call.
    struct SRT_G2P_EXPORT PackageDirectoryDiff {
        std::vector<PackageDirectoryEntry> added;
        std::vector<PackageDirectoryEntry> removed;
        std::vector<PackageDirectoryEntry> unchanged;

        bool operator==(const PackageDirectoryDiff &) const = default;
    };

    /// LanguageService — G2P initialization + route resolution + batch conversion,
    /// plus S2P/Onset discovery.
    ///
    /// G2P Manager is a process-level singleton (initialized by LanguageService).
    /// LanguageService instances are lightweight handles, safe to use across
    /// multiple threads after initialize().
    ///
    /// Two-stage loading (MGR):
    ///   - Stage 1 (initializeMetadata): registers plugin paths, official G2P
    ///     packages, and voicebank G2P contexts. After this call,
    ///     resolveLanguageRoute() / resolveS2pResource() are ready. Does NOT
    ///     load ONNX models.
    ///   - Stage 2 (initializeModels): loads G2P plugin DLLs, creates ONNX
    ///     sessions, and calls Manager::initialize(). After this call,
    ///     convertLyric() / convert() are ready. On failure, G2P conversion is
    ///     disabled but route resolution remains available.
    ///   - initialize() is a convenience wrapper that calls both stages.
    ///
    /// Version isolation (V3-01): the version-aware overloads of
    /// initializeMetadata / resolveLanguageRoute / convert keep multi-version
    /// same-packageId voicebanks isolated across all 5 layers (entry / route /
    /// Manager-context / S2P-cache / convert). The legacy overloads delegate
    /// with an empty version and remain functional for single-version scenarios.
    class SRT_G2P_EXPORT LanguageService {
    public:
        LanguageService();
        ~LanguageService();

        // === Stage 1: Metadata initialization (fast, no ONNX) ===
        //
        // New version-aware entry point (V3-01). Registers G2P plugin paths,
        // official G2P packages, and per-singer voicebank G2P packages keyed
        // by (packageId, version). After this call, resolveLanguageRoute() and
        // resolveS2pResource() are ready (route resolution only needs manifest
        // metadata, not ONNX models). Sets metadataReady() = true on success.
        //
        // Errors:
        //   - PackageDuplicate: same (packageId, version) appears twice in
        //     \p packageDirs (caller should dedupe via VoicebankScanner /
        //     PackageCatalog).
        srt::core::Expected<void> initializeMetadata(
            const std::vector<std::filesystem::path> &pluginSearchPaths,
            const std::vector<std::filesystem::path> &officialG2pPackagePaths,
            const std::vector<PackageDirectoryEntry> &packageDirs);

        /// Incremental metadata update (V3-16 hot reload). Computes a diff against
        /// the currently-registered packageDirs, registers added voicebank G2P
        /// contexts in the Manager, removes retired contexts via
        /// PackageManager::removeContextsByPrefix, and invalidates affected manifest
        /// and S2P cache entries. Existing unchanged packages are not re-registered.
        ///
        /// Requires metadataReady() == true (a prior initializeMetadata() call).
        /// Must NOT be called after initializeModels() — Manager::initialize() makes
        /// contexts immutable; caller must restart the host process for official G2P
        /// or ONNX provider changes.
        ///
        /// Errors:
        ///   - PackageDuplicate: same (packageId, version) appears twice in input
        ///   - G2pInitializationError: Manager::initialize() was already called
        ///     (incremental update requires metadata-only state)
        srt::core::Expected<PackageDirectoryDiff> updateMetadata(
            const std::vector<std::filesystem::path> &pluginSearchPaths,
            const std::vector<std::filesystem::path> &officialG2pPackagePaths,
            const std::vector<PackageDirectoryEntry> &packageDirs);

        // Legacy Stage 1 entry point (deprecated). Internally delegates to the
        // version-aware overload with empty version on each entry; multi-version
        // same-packageId callers will collapse to a single (packageId, empty)
        // entry and produce PackageDuplicate, which is acceptable for the
        // deprecated path — migrate to the version-aware overload.
        [[deprecated("Use the version-aware overload with PackageDirectoryEntry. "
                     "Will be removed in Level=3.")]]
        srt::core::Expected<void> initializeMetadata(
            const std::vector<std::filesystem::path> &pluginSearchPaths,
            const std::vector<std::filesystem::path> &officialG2pPackagePaths,
            const std::unordered_map<std::string, std::filesystem::path> &packageDirs);

        // === Stage 2: Model initialization (slow, loads ONNX) ===
        //
        // Loads G2P plugin DLLs, creates ONNX sessions, and initializes the
        // Manager. On failure, returns an error and the caller should disable
        // G2P conversion (but route resolution remains available). Requires
        // initializeMetadata() to have been called first. Sets modelsReady() =
        // true on success.
        srt::core::Expected<void> initializeModels();

        // === Convenience: initialize = Stage 1 + Stage 2 ===
        //
        // Backward-compatible wrapper that calls initializeMetadata() followed
        // by initializeModels(). Equivalent to the legacy initialize() flow.
        srt::core::Expected<void> initialize(
            const std::vector<std::filesystem::path> &pluginSearchPaths,
            const std::vector<std::filesystem::path> &officialG2pPackagePaths,
            const std::unordered_map<std::string, std::filesystem::path> &packageDirs);

        // === Readiness queries ===
        bool metadataReady() const;  ///< Stage 1 completed
        bool modelsReady() const;    ///< Stage 2 completed
        bool ready() const;          ///< metadataReady() && modelsReady()

        // === Per-singer route ===
        //
        // New version-aware route resolution (V3-01). Resolves G2P route + S2P
        // resource + onset for a (packageId, version, singerId, languageId)
        // tuple. Requires metadataReady() (route resolution uses manifest
        // metadata only, no ONNX models).
        //
        // Version handling:
        //   - Empty version + single packageId match → use it (backward compat
        //     with single-version scenarios).
        //   - Empty version + multiple packageId matches → G2pVersionAmbiguous
        //     error listing all candidate versions/paths.
        //   - Non-empty version → precise routing; G2pPackageNotFound when the
        //     (packageId, version) pair is not in packageDirs.
        //
        // Returned LanguageRoute:
        //   - g2pContext = "packageId__singerId" for voicebank private G2P, or
        //     kOfficialContext for official G2P. ("__" instead of ":" because
        //     ContextUtils::validateContextName forbids ":" — reserved for
        //     FQID separation.)
        //   - g2pContextVersion = the voicebank package version (NOT the G2P
        //     subpackage g2pPackageVersion, which is independent and could
        //     collide between voicebank versions).
        srt::core::Expected<LanguageRoute> resolveLanguageRoute(
            const std::string &packageId,
            const stdc::VersionNumber &version,
            const std::string &singerId,
            const std::string &languageId) const;

        // Legacy 3-arg route resolution (deprecated). Delegates to the
        // version-aware overload with an empty version; multi-version same-
        // packageId scenarios will return G2pVersionAmbiguous.
        [[deprecated("Use the version-aware overload. Will be removed in Level=3.")]]
        srt::core::Expected<LanguageRoute> resolveLanguageRoute(
            const std::string &packageId,
            const std::string &singerId,
            const std::string &languageId) const;

        // === Per-singer S2P resource ===
        //
        // Resolves and caches the S2P LanguageResource for a singer+language.
        // Returns a shared_ptr so the host can call convert() directly.
        // The resource is cached per (packageId, singerId, languageId) tuple;
        // subsequent calls with the same key return the cached resource.
        // Note: this overload has no version parameter — it routes via the
        // single-version path internally. Multi-version callers should use
        // resolveLanguageRoute() + construct LanguageResource directly.
        srt::core::Expected<std::shared_ptr<srt::s2p::LanguageResource>>
        resolveS2pResource(const std::string &packageId,
                           const std::string &singerId,
                           const std::string &languageId) const;

        // === Batch G2P conversion ===
        //
        // Requires modelsReady() (conversion needs the ONNX runtime).
        std::vector<srt::g2p::G2pRes> convertLyric(
            const std::vector<srt::g2p::G2pInput> &input) const;

        // === Convenience: route resolution + batch conversion ===
        //
        // New version-aware overload (V3-01): resolves the route for
        // (packageId, version, singerId, languageId), fills each input's
        // g2pId / g2pContext / g2pContextVersion from the resolved route
        // (LanguageService is the route authority per §2.6 — caller-supplied
        // values are overwritten), then runs convertLyric(). On route
        // resolution failure returns an Expected error; on success returns the
        // G2P result vector (individual results may still carry per-lyric
        // errors via G2pRes::isFailed()). Requires modelsReady().
        srt::core::Expected<std::vector<srt::g2p::G2pRes>> convert(
            const std::string &packageId,
            const stdc::VersionNumber &version,
            const std::string &singerId,
            const std::string &languageId,
            const std::vector<srt::g2p::G2pInput> &inputs) const;

        // Legacy 4-arg convert (deprecated). Delegates to the version-aware
        // overload with an empty version; multi-version same-packageId
        // scenarios will return G2pVersionAmbiguous from route resolution.
        [[deprecated("Use the version-aware overload. Will be removed in Level=3.")]]
        srt::core::Expected<std::vector<srt::g2p::G2pRes>> convert(
            const std::string &packageId,
            const std::string &singerId,
            const std::string &languageId,
            const std::vector<srt::g2p::G2pInput> &inputs) const;

    private:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

} // namespace srt::g2p
