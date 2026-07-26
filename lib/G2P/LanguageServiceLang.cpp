#include <synthrt/G2P/LanguageService.h>

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <stdcorelib/path.h>
#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Support/Logging.h>
#include <synthrt/S2P/LanguageResource.h>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/Core/Manager.h>
#include <synthrt/G2P/Base/LangCommon.h>

#include <diffsinger/Bank/PackageParser.h>
#include <diffsinger/Bank/PackageManifest.h>
#include <diffsinger/Bank/SingerManifest.h>
#include <diffsinger/Bank/LanguageInfo.h>

namespace srt::g2p {

    namespace {
        srt::core::LogCategory langSvcLog("LanguageService");
        bool containsLanguage(const std::vector<ds::bank::LanguageInfo> &items,
                              const std::string &value) {
            return std::find_if(items.begin(), items.end(),
                                [&](const ds::bank::LanguageInfo &l) {
                                    return l.languageId() == value;
                                }) != items.end();
        }

        // Local G2P route fields (internal helper, not affected by R7 field
        // renaming which applies to the public LanguageRoute struct).
        struct G2pRouteData {
            std::string singerId;
            std::string g2pId;
            std::string g2pVersion;
            std::string g2pPackageVersion;
            std::vector<std::filesystem::path> g2pPackages;
            bool voicebankContext = false;
        };

        // Resolves G2P route on an already-parsed package (avoids re-parsing
        // per singer/language in Stage 3).
        srt::core::Expected<G2pRouteData>
        resolveG2pRoute(const ds::bank::PackageManifest &package,
                        const std::string &singerId,
                        const std::string &languageId) {
            const auto singerIt = std::find_if(
                package.singers().begin(), package.singers().end(),
                [&](const ds::bank::SingerManifest &singer) {
                    return singer.singerId() == singerId;
                });
            if (singerIt == package.singers().end()) {
                return srt::core::Error::g2pError(
                    srt::core::ErrorCode::G2pRouteNotFound,
                    "singer not found in package: " + singerId);
            }

            std::string resolvedLanguageId = languageId.empty()
                ? singerIt->defaultLanguage() : languageId;
            if (resolvedLanguageId.empty()) {
                return srt::core::Error::g2pError(
                    srt::core::ErrorCode::G2pValidationError,
                    "language id is required because singer has no defaultLanguage");
            }
            if (!singerIt->languages().empty() &&
                !containsLanguage(singerIt->languages(), resolvedLanguageId)) {
                return srt::core::Error::g2pError(
                    srt::core::ErrorCode::G2pValidationError,
                    "language not declared by singer: " + resolvedLanguageId,
                    resolvedLanguageId);
            }

            const auto languageIt = std::find_if(
                package.languages().begin(), package.languages().end(),
                [&](const ds::bank::LanguageInfo &language) {
                    return language.languageId() == resolvedLanguageId;
                });
            if (languageIt == package.languages().end()) {
                return srt::core::Error::g2pError(
                    srt::core::ErrorCode::G2pRouteNotFound,
                    "language resource not found in package: " + resolvedLanguageId,
                    resolvedLanguageId);
            }

            G2pRouteData route;
            route.singerId = singerIt->singerId();
            route.g2pId = languageIt->g2pId();
            route.g2pVersion = languageIt->g2pVersion();
            if (languageIt->hasG2pPackageVersion()) {
                route.g2pPackageVersion = languageIt->g2pPackageVersion().toString();
            }
            route.voicebankContext = !languageIt->g2pPackages().empty();
            for (const auto &path : languageIt->g2pPackages()) {
                route.g2pPackages.emplace_back(path);
            }
            return route;
        }

        // Builds the voicebank G2P context name for (packageId, singerId).
        // V3-01 §2.4: context = packageId + "__" + singerId (non-empty) so
        // that multi-version same-packageId voicebanks get distinct
        // ContextKeys when paired with the voicebank version. Composed only
        // for voicebank private G2P; official G2P uses kOfficialContext.
        //
        // Separator choice: the spec originally proposed ":" but
        // ContextUtils::validateContextName only allows [A-Za-z0-9_.-] because
        // ":" is reserved for FQID separation ("context@version:moduleId").
        // "__" (double underscore) is allowed, unlikely to appear at the
        // boundary of packageId/singerId in practice, and keeps the context
        // name parseable for diagnostics. The context name itself is opaque
        // (never parsed back — isolation is driven by the (context, version)
        // ContextKey pair, not by splitting the context name).
        std::string voicebankContextName(const std::string &packageId,
                                         const std::string &singerId) {
            return packageId + "__" + singerId;
        }
    } // namespace

    class LanguageService::Impl {
    public:
        // Multi-version storage (V3-01). Key = (packageId, version), value =
        // package directory path. std::map chosen for predictable iteration
        // order (deterministic G2P context registration order, useful for
        // diagnostics). The legacy unordered_map<string, path> collapsed
        // same-packageId entries to the last seen; this map preserves all
        // versions.
        std::map<std::pair<std::string, stdc::VersionNumber>,
                 std::filesystem::path>
            packageDirs;

        // PackageManifest cache (R2+R3). Key = utf8-encoded packageDir path.
        // Avoids re-parsing desc.json on every resolveLanguageRoute() call;
        // Stage 1.3 (voicebank registration) and resolveLanguageRoute() share
        // the same cache. Thread-safe via shared_mutex.
        mutable std::shared_mutex manifestCacheMutex;
        mutable std::unordered_map<std::string, ds::bank::PackageManifest> manifestCache;

        // S2P resource cache (V3-01: key now includes version.toString()).
        // LanguageResource is move-only, so we store shared_ptr for safe
        // shared access across threads.
        mutable std::shared_mutex s2pCacheMutex;
        mutable std::unordered_map<std::string,
                                   std::shared_ptr<srt::s2p::LanguageResource>> s2pCache;

        // Two-stage loading state (MGR).
        bool metadataReady = false;
        bool modelsReady = false;

        // HB-14: pending diagnostics buffer for addPackagePath failures.
        // addPackagePath failures inside initializeMetadata()/updateMetadata()
        // are non-fatal (the call continues with remaining packages) but are
        // recorded here as Warning diagnostics for the caller to drain.
        mutable std::mutex pendingDiagnosticsMutex;
        std::vector<srt::core::Diagnostic> pendingDiagnostics;

        /// Thread-safe append of a pending diagnostic (HB-14).
        void recordPendingDiagnostic(srt::core::Diagnostic d) {
            std::lock_guard<std::mutex> lock(pendingDiagnosticsMutex);
            pendingDiagnostics.push_back(std::move(d));
        }

        /// Find all (version, path) entries for a packageId, in map iteration
        /// order (sorted by version). Empty vector when packageId is unknown.
        std::vector<std::pair<stdc::VersionNumber, std::filesystem::path>>
        findPackages(const std::string &packageId) const {
            std::vector<std::pair<stdc::VersionNumber, std::filesystem::path>> result;
            for (const auto &kv : packageDirs) {
                if (kv.first.first == packageId) {
                    result.emplace_back(kv.first.second, kv.second);
                }
            }
            return result;
        }

        /// Find exact (packageId, version) -> path. Returns nullptr if missing.
        const std::filesystem::path *
        findPackage(const std::string &packageId,
                    const stdc::VersionNumber &version) const {
            const auto it = packageDirs.find({packageId, version});
            if (it == packageDirs.end()) return nullptr;
            return &it->second;
        }

        /// Get or parse the PackageManifest for a (packageId, version) tuple
        /// (thread-safe, cached). Returns a non-owning pointer valid for the
        /// lifetime of this Impl (the cache is never evicted). On parse
        /// failure returns the Error.
        srt::core::Expected<const ds::bank::PackageManifest *>
        getManifest(const std::string &packageId,
                    const stdc::VersionNumber &version) const {
            const auto *pathPtr = findPackage(packageId, version);
            if (!pathPtr) {
                return srt::core::Error::g2pError(
                    srt::core::ErrorCode::G2pPackageNotFound,
                    "package directory not found for packageId=" + packageId +
                        ", version=" + version.toString(),
                    {}, packageId);
            }
            return getManifestByPath(*pathPtr);
        }

        /// Legacy: get manifest for packageId with empty version. Used by the
        /// deprecated code paths where version is unknown. With multi-version
        /// same-packageId entries this returns G2pPackageNotFound — callers
        /// should migrate to the version-aware overload.
        srt::core::Expected<const ds::bank::PackageManifest *>
        getManifest(const std::string &packageId) const {
            return getManifest(packageId, stdc::VersionNumber());
        }

        /// Get or parse manifest by exact directory path (cache key = utf8
        /// path, unchanged from v2). Public on Impl because Impl is a private
        /// inner class of LanguageService — its public surface is only
        /// accessible to LanguageService members.
        srt::core::Expected<const ds::bank::PackageManifest *>
        getManifestByPath(const std::filesystem::path &packageDir) const {
            const auto dirKey = stdc::path::to_utf8(packageDir);

            // Check cache (read lock).
            {
                std::shared_lock<std::shared_mutex> lock(manifestCacheMutex);
                if (const auto it = manifestCache.find(dirKey); it != manifestCache.end()) {
                    return &it->second;
                }
            }

            // Parse and cache (write lock).
            ds::bank::PackageParser parser;
            auto result = parser.parsePackage(packageDir);
            if (!result) {
                return result.takeError();
            }
            std::unique_lock<std::shared_mutex> lock(manifestCacheMutex);
            // Another thread may have populated the entry concurrently; emplace
            // only inserts on miss, so the existing entry (if any) wins.
            auto emplaceResult = manifestCache.emplace(dirKey, std::move(*result));
            return &emplaceResult.first->second;
        }
    };

    LanguageService::LanguageService() : _impl(std::make_unique<Impl>()) {}

    LanguageService::~LanguageService() = default;

    srt::core::Expected<void> LanguageService::initializeMetadata(
        const std::vector<std::filesystem::path> &pluginSearchPaths,
        const std::vector<std::filesystem::path> &officialG2pPackagePaths,
        const std::vector<PackageDirectoryEntry> &packageDirs) {

        // Build the multi-version packageDirs map, detecting duplicate
        // (packageId, version) entries (V3-01 §2.2 / §六). Duplicate detection
        // is the caller's responsibility via VoicebankScanner/PackageCatalog;
        // we still check here to fail fast and avoid silent overwrites.
        _impl->packageDirs.clear();
        for (const auto &entry : packageDirs) {
            const auto key = std::make_pair(entry.packageId, entry.version);
            if (_impl->packageDirs.find(key) != _impl->packageDirs.end()) {
                return srt::core::Error::packageError(
                    srt::core::ErrorCode::PackageDuplicate,
                    "duplicate (packageId, version) in initializeMetadata: packageId=" +
                        entry.packageId + ", version=" + entry.version.toString() +
                        ", path=" + stdc::path::to_utf8(entry.path),
                    entry.packageId);
            }
            _impl->packageDirs.emplace(key, entry.path);
        }

        auto mgr = srt::g2p::Manager::instance();

        // The G2P Manager is a process-wide singleton shared across
        // LanguageService instances. When a previous instance has already
        // initialized it, skip Stage 1.1/1.2 (idempotent) but still run
        // Stage 1.3, which registers this instance's voicebank G2P packages
        // (ER-08).
        const bool alreadyInitialized = mgr->initialized();

        // Diagnostic: log Stage 1.1/1.2 entry conditions. Remove after root
        // cause of "Default context has no modules" is identified.
        langSvcLog.srtInfo(
            "initializeMetadata: alreadyInitialized=%1, pluginPaths=%2, "
            "officialG2pPaths=%3, packageDirs=%4",
            alreadyInitialized, pluginSearchPaths.size(),
            officialG2pPackagePaths.size(), packageDirs.size());

        if (!alreadyInitialized) {
            // Stage 1.1: Register G2P/Driver plugin search paths.
            // addPluginPath scans subdirectories for plugin.json descriptors and is
            // the framework's plugin loading entry point.
            for (const auto &path : pluginSearchPaths) {
                mgr->addPluginPath(srt::g2p::kTaskPluginIid, path);
                mgr->addPluginPath(srt::g2p::kDriverPluginIid, path);
            }

            // Stage 1.2: Register official G2P package paths to the default context.
            // Default-context failures block Manager::initialize().
            for (const auto &path : officialG2pPackagePaths) {
                if (path.empty()) {
                    langSvcLog.srtWarning(
                        "initializeMetadata: skipping empty official G2P path entry");
                    continue;
                }
                auto exp = mgr->addPackagePath(srt::g2p::kOfficialContext, path);
                if (!exp) {
                    return srt::core::Error(
                        srt::core::ErrorCode::G2pSessionError,
                        "Failed to register official G2P package path: " +
                            exp.error().message());
                }
                langSvcLog.srtInfo(
                    "initializeMetadata: registered official G2P path: %1",
                    stdc::path::to_utf8(path));
            }
        }

        // Stage 1.3: Register voicebank private G2P packages for each singer.
        // Always executed: depends on this instance's packageDirs, not the
        // global singleton state. Non-default-context failures only mark the
        // context Failed and do not block other contexts, so per-package errors
        // are reported via srtWarning but do not interrupt the loop (ER-03).
        // Uses the manifest cache (R3) so this shares parse work with
        // resolveLanguageRoute().
        //
        // V3-01 §2.4 context naming change:
        //   - voicebank G2P: context = packageId + "__" + singerId (non-empty)
        //     + version = voicebank package version (non-empty for the new
        //     overload) → versioned ContextKey, satisfies PackageManager R-8/R-9.
        //     ("__" instead of ":" because validateContextName forbids ":" —
        //     reserved for FQID separation. See voicebankContextName() comment.)
        //   - When voicebank version is empty (deprecated path): use the 2-arg
        //     addPackagePath(context, path) overload → unversioned ContextKey
        //     (non-empty context + empty version), also R-8/R-9 compliant.
        //   - official G2P: unchanged (kOfficialContext + 2-arg addPackagePath
        //     registered in Stage 1.2).
        for (const auto &kv : _impl->packageDirs) {
            const auto &packageId = kv.first.first;
            const auto &voicebankVersion = kv.first.second;
            const auto &packageDir = kv.second;

            auto manifestExp = _impl->getManifestByPath(packageDir);
            if (!manifestExp) {
                langSvcLog.srtWarning("failed to parse package for G2P registration: %1: %2",
                                      packageId,
                                      manifestExp.error().message());
                continue;
            }
            const auto &package = **manifestExp;

            for (const auto &singer : package.singers()) {
                for (const auto &lang : singer.languages()) {
                    auto routeExp = resolveG2pRoute(
                        package, singer.singerId(), lang.languageId());
                    if (!routeExp) {
                        langSvcLog.srtWarning("failed to resolve G2P route for singer %1, lang %2: %3",
                                              singer.singerId(), lang.languageId(),
                                              routeExp.error().message());
                        continue;
                    }
                    const auto &route = *routeExp;
                    if (!route.voicebankContext) {
                        continue;
                    }

                    const auto context = voicebankContextName(packageId, route.singerId);
                    for (const auto &g2pPath : route.g2pPackages) {
                        if (voicebankVersion.isEmpty()) {
                            // Deprecated path (empty voicebank version):
                            // unversioned context — non-empty context + empty
                            // version via the 2-arg overload.
                            auto addExp = mgr->addPackagePath(context, g2pPath);
                            if (!addExp) {
                                langSvcLog.srtWarning("addPackagePath failed for context %1, path %2: %3",
                                                      context,
                                                      stdc::path::to_utf8(g2pPath),
                                                      addExp.error().message());
                                // HB-14: record as Warning diagnostic for callers.
                                srt::core::Diagnostic _d;
                                _d.code = addExp.error().code();
                                _d.severity = srt::core::Severity::Warning;
                                _d.message = "addPackagePath failed: " + addExp.error().message();
                                _d.extraContext = {{"context", context},
                                                   {"g2pPath", stdc::path::to_utf8(g2pPath)}};
                                _impl->recordPendingDiagnostic(std::move(_d));
                            }
                        } else {
                            // Version-aware path: versioned context — non-empty
                            // context + non-empty voicebank version.
                            auto addExp = mgr->addPackagePath(context, voicebankVersion, g2pPath);
                            if (!addExp) {
                                langSvcLog.srtWarning("addPackagePath failed for context %1, version %2, path %3: %4",
                                                      context, voicebankVersion.toString(),
                                                      stdc::path::to_utf8(g2pPath),
                                                      addExp.error().message());
                                // HB-14: record as Warning diagnostic for callers.
                                srt::core::Diagnostic _d;
                                _d.code = addExp.error().code();
                                _d.severity = srt::core::Severity::Warning;
                                _d.message = "addPackagePath failed: " + addExp.error().message();
                                _d.extraContext = {{"context", context},
                                                   {"version", voicebankVersion.toString()},
                                                   {"g2pPath", stdc::path::to_utf8(g2pPath)}};
                                _impl->recordPendingDiagnostic(std::move(_d));
                            }
                        }
                    }
                }
            }
        }

        _impl->metadataReady = true;
        return {};
    }

    srt::core::Expected<PackageDirectoryDiff> LanguageService::updateMetadata(
        const std::vector<std::filesystem::path> &pluginSearchPaths,
        const std::vector<std::filesystem::path> &officialG2pPackagePaths,
        const std::vector<PackageDirectoryEntry> &packageDirs) {

        // V3-16 hot reload: incremental metadata update. The plugin search
        // paths and official G2P package paths are accepted for API symmetry
        // with initializeMetadata() but are NOT applied here — changing them
        // after the Manager singleton has seen them requires a process restart
        // (documented hot-reload limitation). Only voicebank G2P contexts are
        // updated incrementally.
        (void)pluginSearchPaths;
        (void)officialG2pPackagePaths;

        // Guard: must be in metadata-only state (no initializeModels yet).
        // Manager::initialize() makes contexts immutable; an incremental update
        // after that would leave retired contexts in place and silently fail to
        // register new ones, so we reject the call up front.
        if (_impl->modelsReady) {
            return srt::core::Error(
                srt::core::ErrorCode::G2pAlreadyInitialized,
                "updateMetadata: Manager::initialize() already called; "
                "incremental update requires metadata-only state. "
                "Restart the host process for G2P changes.");
        }
        if (!_impl->metadataReady) {
            return srt::core::Error(
                srt::core::ErrorCode::G2pInitializationError,
                "updateMetadata: initializeMetadata() must be called first.");
        }

        // 1. Build the new packageDirs map with duplicate detection. Duplicate
        //    (packageId, version) pairs in the input are a caller bug; fail
        //    fast rather than silently overwriting.
        std::map<std::pair<std::string, stdc::VersionNumber>, std::filesystem::path> next;
        for (const auto &entry : packageDirs) {
            const auto key = std::make_pair(entry.packageId, entry.version);
            if (next.find(key) != next.end()) {
                return srt::core::Error::packageError(
                    srt::core::ErrorCode::PackageDuplicate,
                    "duplicate (packageId, version) in updateMetadata: packageId=" +
                        entry.packageId + ", version=" + entry.version.toString(),
                    entry.packageId);
            }
            next.emplace(key, entry.path);
        }

        // 2. Compute the diff against the currently-registered packageDirs.
        //    Map iteration order is deterministic (std::map keyed by
        //    (string, VersionNumber)), so diff.added / diff.removed /
        //    diff.unchanged are deterministic for diagnostics and tests.
        //    BUG-G2P-009: diff must compare path as well as (packageId,
        //    version). When a voicebank migrates to a different path with
        //    the same (packageId, version), treat it as removed + added so
        //    removeContextsByPrefix fires for the old path and the new path
        //    is registered; otherwise the hot reload silently no-ops.
        PackageDirectoryDiff diff;
        for (const auto &kv : next) {
            auto it = _impl->packageDirs.find(kv.first);
            if (it == _impl->packageDirs.end()) {
                diff.added.push_back(
                    {kv.first.first, kv.first.second, kv.second});
            } else if (it->second != kv.second) {
                // path 变化：视为 removed（旧 path）+ added（新 path）
                diff.removed.push_back(
                    {it->first.first, it->first.second, it->second});
                diff.added.push_back(
                    {kv.first.first, kv.first.second, kv.second});
            } else {
                diff.unchanged.push_back(
                    {kv.first.first, kv.first.second, kv.second});
            }
        }
        for (const auto &kv : _impl->packageDirs) {
            if (next.find(kv.first) == next.end()) {
                diff.removed.push_back(
                    {kv.first.first, kv.first.second, kv.second});
            }
        }

        auto mgr = srt::g2p::Manager::instance();

        // 3. Remove retired voicebank G2P contexts. The context name is
        //    "packageId__singerId" (see voicebankContextName), so the prefix
        //    "packageId__" matches every singer under that packageId.
        //    D-43: pass the retired entry's version to the version-aware
        //    removeContextsByPrefix overload so coexisting versions of the
        //    same packageId are NOT retired. The single-arg overload would
        //    match every version, corrupting multi-version coexistence
        //    (D-24 violation). Failures here are non-fatal: a leftover
        //    context is unused once packageDirs no longer references it, so
        //    we log and continue with the remaining removals.
        for (const auto &entry : diff.removed) {
            const auto prefix = entry.packageId + "__";
            auto rmExp = mgr->removeContextsByPrefix(prefix, entry.version);
            if (!rmExp) {
                langSvcLog.srtWarning("removeContextsByPrefix failed for prefix %1, version %2: %3",
                                      prefix, entry.version.toString(),
                                      rmExp.error().message());
            }
        }

        // 4. Invalidate manifest cache and S2P cache for removed entries.
        //    Manifest cache key = utf8 package dir path; S2P cache key =
        //    "packageId/version/singer/lang". Erasing forces a re-parse / re-
        //    build on next access and prevents stale entries from surfacing
        //    after a package is replaced or removed.
        {
            std::unique_lock<std::shared_mutex> lock(_impl->manifestCacheMutex);
            for (const auto &entry : diff.removed) {
                const auto key = stdc::path::to_utf8(entry.path);
                _impl->manifestCache.erase(key);
            }
        }
        {
            std::unique_lock<std::shared_mutex> lock(_impl->s2pCacheMutex);
            for (const auto &entry : diff.removed) {
                const auto prefix =
                    entry.packageId + "/" + entry.version.toString() + "/";
                for (auto it = _impl->s2pCache.begin();
                     it != _impl->s2pCache.end();) {
                    if (it->first.size() >= prefix.size() &&
                        it->first.compare(0, prefix.size(), prefix) == 0) {
                        it = _impl->s2pCache.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        }

        // 5. Register added voicebank G2P contexts (mirrors Stage 1.3 of
        //    initializeMetadata). Per-package errors are logged and do not
        //    interrupt the loop (ER-03: non-default-context failures only
        //    mark the context Failed). The manifest cache is shared with
        //    resolveLanguageRoute(), so added entries become immediately
        //    resolvable once this call returns.
        for (const auto &entry : diff.added) {
            auto manifestExp = _impl->getManifestByPath(entry.path);
            if (!manifestExp) {
                langSvcLog.srtWarning("failed to parse package for G2P registration: %1: %2",
                                      entry.packageId,
                                      manifestExp.error().message());
                continue;
            }
            const auto &package = **manifestExp;

            for (const auto &singer : package.singers()) {
                for (const auto &lang : singer.languages()) {
                    auto routeExp = resolveG2pRoute(
                        package, singer.singerId(), lang.languageId());
                    if (!routeExp) {
                        langSvcLog.srtWarning("failed to resolve G2P route for singer %1, lang %2: %3",
                                              singer.singerId(), lang.languageId(),
                                              routeExp.error().message());
                        continue;
                    }
                    const auto &route = *routeExp;
                    if (!route.voicebankContext) {
                        continue;
                    }

                    const auto context =
                        voicebankContextName(entry.packageId, route.singerId);
                    for (const auto &g2pPath : route.g2pPackages) {
                        if (entry.version.isEmpty()) {
                            // Deprecated path (empty voicebank version):
                            // unversioned context — non-empty context + empty
                            // version via the 2-arg overload.
                            auto addExp = mgr->addPackagePath(context, g2pPath);
                            if (!addExp) {
                                langSvcLog.srtWarning("addPackagePath failed for context %1, path %2: %3",
                                                      context,
                                                      stdc::path::to_utf8(g2pPath),
                                                      addExp.error().message());
                                // HB-14: record as Warning diagnostic for callers.
                                srt::core::Diagnostic _d;
                                _d.code = addExp.error().code();
                                _d.severity = srt::core::Severity::Warning;
                                _d.message = "addPackagePath failed: " + addExp.error().message();
                                _d.extraContext = {{"context", context},
                                                   {"g2pPath", stdc::path::to_utf8(g2pPath)}};
                                _impl->recordPendingDiagnostic(std::move(_d));
                            }
                        } else {
                            // Version-aware path: versioned context — non-empty
                            // context + non-empty voicebank version.
                            auto addExp = mgr->addPackagePath(context, entry.version, g2pPath);
                            if (!addExp) {
                                langSvcLog.srtWarning("addPackagePath failed for context %1, version %2, path %3: %4",
                                                      context, entry.version.toString(),
                                                      stdc::path::to_utf8(g2pPath),
                                                      addExp.error().message());
                                // HB-14: record as Warning diagnostic for callers.
                                srt::core::Diagnostic _d;
                                _d.code = addExp.error().code();
                                _d.severity = srt::core::Severity::Warning;
                                _d.message = "addPackagePath failed: " + addExp.error().message();
                                _d.extraContext = {{"context", context},
                                                   {"version", entry.version.toString()},
                                                   {"g2pPath", stdc::path::to_utf8(g2pPath)}};
                                _impl->recordPendingDiagnostic(std::move(_d));
                            }
                        }
                    }
                }
            }
        }

        // 6. Swap in the new packageDirs. From this point resolveLanguageRoute
        //    / findPackages / findPackage observe the new set.
        _impl->packageDirs = std::move(next);

        return diff;
    }

    srt::core::Expected<void> LanguageService::initializeMetadata(
        const std::vector<std::filesystem::path> &pluginSearchPaths,
        const std::vector<std::filesystem::path> &officialG2pPackagePaths,
        const std::unordered_map<std::string, std::filesystem::path> &packageDirs) {

        // Deprecated path (V3-01 §2.2): build a vector<PackageDirectoryEntry>
        // with empty version for each map entry and delegate to the new
        // overload. Multi-version same-packageId callers have already
        // collapsed to one entry in the map (last wins), so this is silently
        // lossy — callers should migrate to the version-aware overload.
        std::vector<PackageDirectoryEntry> entries;
        entries.reserve(packageDirs.size());
        for (const auto &kv : packageDirs) {
            entries.push_back(PackageDirectoryEntry{kv.first, stdc::VersionNumber(), kv.second});
        }
        return initializeMetadata(pluginSearchPaths, officialG2pPackagePaths, entries);
    }

    srt::core::Expected<void> LanguageService::initializeModels() {
        langSvcLog.srtInfo("initializeModels: START metadataReady=%1 modelsReady=%2",
                           _impl->metadataReady, _impl->modelsReady);
        if (!_impl->metadataReady) {
            langSvcLog.srtWarning("initializeModels: metadata not ready");
            return srt::core::Error(
                srt::core::ErrorCode::G2pInitializationError,
                "initializeModels() requires initializeMetadata() first");
        }

        auto mgr = srt::g2p::Manager::instance();
        if (mgr->initialized()) {
            // Idempotent: already initialized by another LanguageService instance.
            langSvcLog.srtInfo("initializeModels: Manager already initialized, marking modelsReady");
            _impl->modelsReady = true;
            return {};
        }

        // Stage 2: Initialize all registered G2P contexts (load plugins, create
        // ONNX sessions). Failure terminates G2P conversion but route resolution
        // (Stage 1) remains available.
        langSvcLog.srtInfo("initializeModels: calling Manager::initialize()");
        auto g2pResult = mgr->initialize();
        if (!g2pResult) {
            // G2pAlreadyInitialized is not a real failure: another thread won
            // the init race while we were waiting for the init mutex. The
            // Manager is now initialized, so we can proceed.
            if (g2pResult.error().code() == srt::core::ErrorCode::G2pAlreadyInitialized) {
                langSvcLog.srtInfo("initializeModels: Manager already initialized by another thread, marking modelsReady");
                _impl->modelsReady = true;
                return {};
            }
            langSvcLog.srtWarning("initializeModels: Manager::initialize FAILED: %1",
                                  g2pResult.error().message());
            return srt::core::Error(
                srt::core::ErrorCode::G2pInitializationError,
                "G2P Manager initialization failed: " +
                    g2pResult.error().message());
        }

        langSvcLog.srtInfo("initializeModels: Manager::initialize succeeded, marking modelsReady");
        _impl->modelsReady = true;
        return {};
    }

    srt::core::Expected<void> LanguageService::initialize(
        const std::vector<std::filesystem::path> &pluginSearchPaths,
        const std::vector<std::filesystem::path> &officialG2pPackagePaths,
        const std::unordered_map<std::string, std::filesystem::path> &packageDirs) {

        // Delegate through the deprecated initializeMetadata overload; it
        // forwards to the version-aware overload with empty version. This
        // wrapper itself is not deprecated (legacy hosts call it), so the
        // deprecation warning is intentionally emitted only at the direct
        // call sites of initializeMetadata(map).
        auto exp1 = initializeMetadata(pluginSearchPaths, officialG2pPackagePaths, packageDirs);
        if (!exp1) {
            return exp1;
        }
        return initializeModels();
    }

    bool LanguageService::metadataReady() const {
        return _impl->metadataReady;
    }

    bool LanguageService::modelsReady() const {
        return _impl->modelsReady;
    }

    bool LanguageService::ready() const {
        return _impl->metadataReady && _impl->modelsReady;
    }

    std::vector<srt::core::Diagnostic> LanguageService::drainPendingDiagnostics() {
        std::lock_guard<std::mutex> lock(_impl->pendingDiagnosticsMutex);
        std::vector<srt::core::Diagnostic> out;
        out.swap(_impl->pendingDiagnostics);
        return out;
    }

    srt::core::Expected<LanguageRoute> LanguageService::resolveLanguageRoute(
        const std::string &packageId,
        const stdc::VersionNumber &version,
        const std::string &singerId,
        const std::string &languageId) const {

        // V3-01 §2.5 route resolution strategy.
        // Resolve the package directory (and effective voicebank version)
        // according to the requested version.
        std::filesystem::path packageDir;
        stdc::VersionNumber effectiveVersion = version;
        if (version.isEmpty()) {
            // Empty version: enumerate all entries for this packageId.
            const auto candidates = _impl->findPackages(packageId);
            if (candidates.empty()) {
                return srt::core::Error::g2pError(
                    srt::core::ErrorCode::G2pPackageNotFound,
                    "package directory not found for packageId=" + packageId,
                    {}, packageId);
            }
            if (candidates.size() > 1) {
                // V3-10: ambiguous — list all candidate versions+paths.
                std::string msg =
                    "packageId=" + packageId + " has multiple versions; "
                    "provide a version to disambiguate. Candidates: ";
                for (size_t i = 0; i < candidates.size(); ++i) {
                    if (i != 0) msg += ", ";
                    msg += candidates[i].first.toString() + " (" +
                           stdc::path::to_utf8(candidates[i].second) + ")";
                }
                return srt::core::Error::g2pError(
                    srt::core::ErrorCode::G2pVersionAmbiguous,
                    std::move(msg), {}, packageId);
            }
            // Single match — use it (backward compat with single-version).
            packageDir = candidates.front().second;
            effectiveVersion = candidates.front().first;
        } else {
            // Non-empty version: precise lookup.
            const auto *pathPtr = _impl->findPackage(packageId, version);
            if (!pathPtr) {
                return srt::core::Error::g2pError(
                    srt::core::ErrorCode::G2pPackageNotFound,
                    "package directory not found for packageId=" + packageId +
                        ", version=" + version.toString(),
                    {}, packageId);
            }
            packageDir = *pathPtr;
        }

        // Use the manifest cache (R2): parse once, reuse on subsequent calls.
        auto manifestExp = _impl->getManifestByPath(packageDir);
        if (!manifestExp) {
            return manifestExp.takeError();
        }
        const auto &package = **manifestExp;

        const auto singerIt = std::find_if(
            package.singers().begin(), package.singers().end(),
            [&](const ds::bank::SingerManifest &singer) {
                return singer.singerId() == singerId;
            });
        if (singerIt == package.singers().end()) {
            return srt::core::Error::g2pError(
                srt::core::ErrorCode::G2pRouteNotFound,
                "singer not found in package: " + singerId,
                {}, packageId);
        }

        std::string resolvedLanguageId = languageId.empty()
            ? singerIt->defaultLanguage() : languageId;
        if (resolvedLanguageId.empty()) {
            return srt::core::Error::g2pError(
                srt::core::ErrorCode::G2pValidationError,
                "language id is required because singer has no defaultLanguage",
                {}, packageId);
        }
        if (!singerIt->languages().empty() &&
            !containsLanguage(singerIt->languages(), resolvedLanguageId)) {
            return srt::core::Error::g2pError(
                srt::core::ErrorCode::G2pValidationError,
                "language not declared by singer: " + resolvedLanguageId,
                resolvedLanguageId, packageId);
        }

        const auto languageIt = std::find_if(
            package.languages().begin(), package.languages().end(),
            [&](const ds::bank::LanguageInfo &language) {
                return language.languageId() == resolvedLanguageId;
            });
        if (languageIt == package.languages().end()) {
            return srt::core::Error::g2pError(
                srt::core::ErrorCode::G2pRouteNotFound,
                "language resource not found in package: " + resolvedLanguageId,
                resolvedLanguageId, packageId);
        }

        const bool hasG2pPackages = !languageIt->g2pPackages().empty();

        LanguageRoute result;
        result.g2pId = languageIt->g2pId();
        // V3-01 §2.4 context naming:
        //   - voicebank private G2P: context = "packageId__singerId" (non-empty)
        //     so multi-version same-packageId voicebanks get distinct
        //     ContextKeys when paired with the voicebank version.
        //   - official G2P: context = kOfficialContext (empty).
        result.g2pContext = hasG2pPackages
            ? voicebankContextName(packageId, singerIt->singerId())
            : srt::g2p::kOfficialContext;
        // V3-01 §2.4: g2pContextVersion = voicebank package version (NOT the
        // G2P subpackage g2pPackageVersion, which is independent and could
        // collide between voicebank versions). For official G2P the version
        // is empty (kOfficialContext is unversioned).
        result.g2pContextVersion = hasG2pPackages
            ? effectiveVersion
            : stdc::VersionNumber();
        // g2pSource: "voicebank" when voicebank private G2P packages exist,
        // "official" otherwise (R7).
        result.g2pSource = hasG2pPackages ? srt::g2p::kG2pSourceVoicebank
                                         : srt::g2p::kG2pSourceOfficial;
        result.s2pMode = languageIt->s2pMode();
        result.s2pFile = !languageIt->s2pFile().empty()
            ? languageIt->s2pFile() : languageIt->dict();
        result.onsetFile = languageIt->onsetFile();
        return result;
    }

    srt::core::Expected<LanguageRoute> LanguageService::resolveLanguageRoute(
        const std::string &packageId,
        const std::string &singerId,
        const std::string &languageId) const {
        // Deprecated path (V3-01 §2.3): delegate to the version-aware overload
        // with an empty version. Multi-version same-packageId scenarios return
        // G2pVersionAmbiguous from the new overload.
        return resolveLanguageRoute(packageId, stdc::VersionNumber(), singerId, languageId);
    }

    srt::core::Expected<std::shared_ptr<srt::s2p::LanguageResource>>
    LanguageService::resolveS2pResource(const std::string &packageId,
                                        const std::string &singerId,
                                        const std::string &languageId) const {
        // Deprecated path (V3-01 §2.4): delegate to the version-aware
        // overload with an empty version. Multi-version same-packageId
        // scenarios return G2pVersionAmbiguous from the new overload's call
        // to resolveLanguageRoute.
        return resolveS2pResource(packageId, stdc::VersionNumber(),
                                  singerId, languageId);
    }

    srt::core::Expected<std::shared_ptr<srt::s2p::LanguageResource>>
    LanguageService::resolveS2pResource(const std::string &packageId,
                                        const stdc::VersionNumber &version,
                                        const std::string &singerId,
                                        const std::string &languageId) const {
        // V3-01 §2.4: S2P cache key includes version.toString() so that
        // multi-version same-packageId voicebanks get independent cache slots.
        // Empty version produces "pkg//singer/lang" (backward compat with
        // single-version scenarios via the deprecated overload).
        const auto key = packageId + "/" + version.toString() +
                         "/" + singerId + "/" + languageId;

        // Check cache (read lock).
        {
            std::shared_lock<std::shared_mutex> lock(_impl->s2pCacheMutex);
            const auto it = _impl->s2pCache.find(key);
            if (it != _impl->s2pCache.end()) {
                return it->second;
            }
        }

        // Resolve route via the version-aware resolveLanguageRoute. Empty
        // version + multiple matches returns G2pVersionAmbiguous; non-empty
        // version routes precisely (V3-01 §2.5).
        auto routeExp = resolveLanguageRoute(packageId, version, singerId, languageId);
        if (!routeExp) {
            return routeExp.takeError();
        }
        const auto &route = *routeExp;

        if (route.s2pMode.empty()) {
            return srt::core::Error(
                srt::core::ErrorCode::S2pResourceNotFound,
                "S2P mode is empty (no S2P resource configured) for language: " +
                    languageId);
        }

        std::shared_ptr<srt::s2p::LanguageResource> resource;
        try {
            if (route.s2pMode == "dict" && !route.s2pFile.empty()) {
                resource = std::make_shared<srt::s2p::LanguageResource>(
                    srt::s2p::LanguageResource::dictionary(
                        stdc::path::to_utf8(route.s2pFile),
                        stdc::path::to_utf8(route.onsetFile)));
            } else {
                resource = std::make_shared<srt::s2p::LanguageResource>(
                    srt::s2p::LanguageResource::direct(
                        stdc::path::to_utf8(route.onsetFile)));
            }
        } catch (const std::exception &e) {
            return srt::core::Error(
                srt::core::ErrorCode::S2pResourceNotFound,
                "S2P resource construction failed (s2pMode=" + route.s2pMode +
                    ", s2pFile=" + stdc::path::to_utf8(route.s2pFile) +
                    "): " + e.what());
        }

        // Cache (write lock).
        {
            std::unique_lock<std::shared_mutex> lock(_impl->s2pCacheMutex);
            _impl->s2pCache.emplace(key, resource);
        }

        return resource;
    }

    std::vector<srt::g2p::G2pRes> LanguageService::convertLyric(
        const std::vector<srt::g2p::G2pInput> &input) const {
        return srt::g2p::Manager::instance()->convert(input);
    }

    srt::core::Expected<std::vector<srt::g2p::G2pRes>> LanguageService::convert(
        const std::string &packageId,
        const stdc::VersionNumber &version,
        const std::string &singerId,
        const std::string &languageId,
        const std::vector<srt::g2p::G2pInput> &inputs) const {
        // V3-01 §2.6: LanguageService is the route authority — fill each
        // input's g2pId / g2pContext / g2pContextVersion from the resolved
        // route, overwriting caller-supplied values, so Manager::convert can
        // route to the correct context.
        auto routeExp = resolveLanguageRoute(packageId, version, singerId, languageId);
        if (!routeExp) {
            return routeExp.takeError();
        }
        const auto &route = *routeExp;

        std::vector<G2pInput> routed = inputs;
        for (auto &in : routed) {
            in.g2pId = route.g2pId;
            in.g2pContext = route.g2pContext;
            in.g2pContextVersion = route.g2pContextVersion;
        }
        // Conversion results may contain per-lyric errors (G2pRes::isFailed());
        // callers inspect each result rather than treating the whole call as
        // failed (R6: don't lose error details by collapsing to bool).
        return convertLyric(routed);
    }

    srt::core::Expected<std::vector<srt::g2p::G2pRes>> LanguageService::convert(
        const std::string &packageId,
        const std::string &singerId,
        const std::string &languageId,
        const std::vector<srt::g2p::G2pInput> &inputs) const {
        // Deprecated path (V3-01 §2.6): delegate to the version-aware overload
        // with an empty version.
        return convert(packageId, stdc::VersionNumber(), singerId, languageId, inputs);
    }

} // namespace srt::g2p
