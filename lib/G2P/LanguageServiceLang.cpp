#include <synthrt/G2P/LanguageService.h>

#include <algorithm>
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
    } // namespace

    class LanguageService::Impl {
    public:
        std::unordered_map<std::string, std::filesystem::path> packageDirs;

        // PackageManifest cache (R2+R3). Key = utf8-encoded packageDir path.
        // Avoids re-parsing desc.json on every resolveLanguageRoute() call;
        // Stage 1.3 (voicebank registration) and resolveLanguageRoute() share
        // the same cache. Thread-safe via shared_mutex.
        mutable std::shared_mutex manifestCacheMutex;
        mutable std::unordered_map<std::string, ds::bank::PackageManifest> manifestCache;

        // S2P resource cache (keyed by "packageId/singerId/languageId").
        // LanguageResource is move-only, so we store shared_ptr for safe
        // shared access across threads.
        mutable std::shared_mutex s2pCacheMutex;
        mutable std::unordered_map<std::string,
                                   std::shared_ptr<srt::s2p::LanguageResource>> s2pCache;

        // Two-stage loading state (MGR).
        bool metadataReady = false;
        bool modelsReady = false;

        /// Get or parse the PackageManifest for a packageId (thread-safe, cached).
        /// Returns a non-owning pointer valid for the lifetime of this Impl
        /// (the cache is never evicted). On parse failure returns the Error.
        srt::core::Expected<const ds::bank::PackageManifest *>
        getManifest(const std::string &packageId) const {
            const auto dirIt = packageDirs.find(packageId);
            if (dirIt == packageDirs.end()) {
                return srt::core::Error::g2pError(
                    srt::core::ErrorCode::G2pPackageNotFound,
                    "package directory not found for packageId: " + packageId,
                    {}, packageId);
            }
            const auto &packageDir = dirIt->second;
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
        const std::unordered_map<std::string, std::filesystem::path> &packageDirs) {

        // Store packageDirs for later resolveLanguageRoute() / convert() calls.
        _impl->packageDirs = packageDirs;

        auto mgr = srt::g2p::Manager::instance();

        // The G2P Manager is a process-wide singleton shared across
        // LanguageService instances. When a previous instance has already
        // initialized it, skip Stage 1.1/1.2 (idempotent) but still run
        // Stage 1.3, which registers this instance's voicebank G2P packages
        // (ER-08).
        const bool alreadyInitialized = mgr->initialized();

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
                    continue;
                }
                auto exp = mgr->addPackagePath(srt::g2p::kOfficialContext, path);
                if (!exp) {
                    return srt::core::Error(
                        srt::core::ErrorCode::G2pSessionError,
                        "Failed to register official G2P package path: " +
                            exp.error().message());
                }
            }
        }

        // Stage 1.3: Register voicebank private G2P packages for each singer.
        // Always executed: depends on this instance's packageDirs, not the
        // global singleton state. Non-default-context failures only mark the
        // context Failed and do not block other contexts, so per-package errors
        // are reported via srtWarning but do not interrupt the loop (ER-03).
        // Uses the manifest cache (R3) so this shares parse work with
        // resolveLanguageRoute().
        for (const auto &kv : _impl->packageDirs) {
            const auto &packageId = kv.first;

            auto manifestExp = _impl->getManifest(packageId);
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

                    const auto version = route.g2pPackageVersion.empty()
                        ? stdc::VersionNumber()
                        : stdc::VersionNumber::fromString(route.g2pPackageVersion);
                    for (const auto &g2pPath : route.g2pPackages) {
                        if (version.isEmpty()) {
                            auto addExp = mgr->addPackagePath(route.singerId, g2pPath);
                            if (!addExp) {
                                langSvcLog.srtWarning("addPackagePath failed for singer %1, path %2: %3",
                                                      route.singerId,
                                                      stdc::path::to_utf8(g2pPath),
                                                      addExp.error().message());
                            }
                        } else {
                            auto addExp = mgr->addPackagePath(route.singerId, version, g2pPath);
                            if (!addExp) {
                                langSvcLog.srtWarning("addPackagePath failed for singer %1, version %2, path %3: %4",
                                                      route.singerId, route.g2pPackageVersion,
                                                      stdc::path::to_utf8(g2pPath),
                                                      addExp.error().message());
                            }
                        }
                    }
                }
            }
        }

        _impl->metadataReady = true;
        return {};
    }

    srt::core::Expected<void> LanguageService::initializeModels() {
        if (!_impl->metadataReady) {
            return srt::core::Error(
                srt::core::ErrorCode::G2pInitializationError,
                "initializeModels() requires initializeMetadata() first");
        }

        auto mgr = srt::g2p::Manager::instance();
        if (mgr->initialized()) {
            // Idempotent: already initialized by another LanguageService instance.
            _impl->modelsReady = true;
            return {};
        }

        // Stage 2: Initialize all registered G2P contexts (load plugins, create
        // ONNX sessions). Failure terminates G2P conversion but route resolution
        // (Stage 1) remains available.
        auto g2pResult = mgr->initialize();
        if (!g2pResult) {
            return srt::core::Error(
                srt::core::ErrorCode::G2pInitializationError,
                "G2P Manager initialization failed: " +
                    g2pResult.error().message());
        }

        _impl->modelsReady = true;
        return {};
    }

    srt::core::Expected<void> LanguageService::initialize(
        const std::vector<std::filesystem::path> &pluginSearchPaths,
        const std::vector<std::filesystem::path> &officialG2pPackagePaths,
        const std::unordered_map<std::string, std::filesystem::path> &packageDirs) {

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

    srt::core::Expected<LanguageRoute> LanguageService::resolveLanguageRoute(
        const std::string &packageId,
        const std::string &singerId,
        const std::string &languageId) const {

        // Use the manifest cache (R2): parse once, reuse on subsequent calls.
        auto manifestExp = _impl->getManifest(packageId);
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
        // g2pContext: empty (= kOfficialContext) for official G2P, singerId for
        // voicebank private G2P (R7).
        result.g2pContext = hasG2pPackages ? singerIt->singerId()
                                          : srt::g2p::kOfficialContext;
        result.g2pContextVersion = languageIt->hasG2pPackageVersion()
            ? languageIt->g2pPackageVersion()
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

    srt::core::Expected<std::shared_ptr<srt::s2p::LanguageResource>>
    LanguageService::resolveS2pResource(const std::string &packageId,
                                        const std::string &singerId,
                                        const std::string &languageId) const {
        const auto key = packageId + "/" + singerId + "/" + languageId;

        // Check cache (read lock).
        {
            std::shared_lock<std::shared_mutex> lock(_impl->s2pCacheMutex);
            const auto it = _impl->s2pCache.find(key);
            if (it != _impl->s2pCache.end()) {
                return it->second;
            }
        }

        // Resolve route (reuses resolveLanguageRoute logic).
        auto routeExp = resolveLanguageRoute(packageId, singerId, languageId);
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
        const std::string &singerId,
        const std::string &languageId,
        const std::vector<srt::g2p::G2pInput> &inputs) const {
        // Route resolution failure → propagate the error.
        auto routeExp = resolveLanguageRoute(packageId, singerId, languageId);
        if (!routeExp) {
            return routeExp.takeError();
        }
        // Conversion results may contain per-lyric errors (G2pRes::isFailed());
        // callers inspect each result rather than treating the whole call as
        // failed (R6: don't lose error details by collapsing to bool).
        return convertLyric(inputs);
    }

} // namespace srt::g2p
