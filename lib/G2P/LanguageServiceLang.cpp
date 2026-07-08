#include <synthrt/G2P/LanguageService.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <stdcorelib/path.h>
#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/Core/Manager.h>
#include <synthrt/G2P/Base/LangCommon.h>

#include <diffsinger/Bank/PackageParser.h>
#include <diffsinger/Bank/PackageManifest.h>
#include <diffsinger/Bank/SingerManifest.h>
#include <diffsinger/Bank/LanguageInfo.h>

namespace ds::lang {

    namespace {
        bool containsLanguage(const std::vector<ds::bank::LanguageInfo> &items,
                              const std::string &value) {
            return std::find_if(items.begin(), items.end(),
                                [&](const ds::bank::LanguageInfo &l) {
                                    return l.languageId() == value;
                                }) != items.end();
        }

        // Local G2P route fields.
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
                return srt::core::Error(
                    srt::core::Error::InvalidArgument,
                    "singer not found in package: " + singerId);
            }

            std::string resolvedLanguageId = languageId.empty()
                ? singerIt->defaultLanguage() : languageId;
            if (resolvedLanguageId.empty()) {
                return srt::core::Error(
                    srt::core::Error::InvalidArgument,
                    "language id is required because singer has no defaultLanguage");
            }
            if (!singerIt->languages().empty() &&
                !containsLanguage(singerIt->languages(), resolvedLanguageId)) {
                return srt::core::Error(
                    srt::core::Error::InvalidArgument,
                    "language not declared by singer: " + resolvedLanguageId);
            }

            const auto languageIt = std::find_if(
                package.languages().begin(), package.languages().end(),
                [&](const ds::bank::LanguageInfo &language) {
                    return language.languageId() == resolvedLanguageId;
                });
            if (languageIt == package.languages().end()) {
                return srt::core::Error(
                    srt::core::Error::InvalidArgument,
                    "language resource not found in package: " + resolvedLanguageId);
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
    };

    LanguageService::LanguageService() : _impl(std::make_unique<Impl>()) {}

    LanguageService::~LanguageService() = default;

    srt::core::Expected<void> LanguageService::initialize(
        const std::vector<std::filesystem::path> &pluginSearchPaths,
        const std::vector<std::filesystem::path> &officialG2pPackagePaths,
        const std::unordered_map<std::string, std::filesystem::path> &packageDirs) {

        // Store packageDirs for later resolveLanguageRoute() / convert() calls.
        _impl->packageDirs = packageDirs;

        auto mgr = srt::g2p::Manager::instance();

        // The G2P Manager is a process-wide singleton shared across
        // LanguageService instances. When a previous instance has already
        // initialized it, skip Stage 1/2/4 (idempotent) but still run Stage 3,
        // which registers this instance's voicebank G2P packages (ER-08).
        const bool alreadyInitialized = mgr->initialized();

        if (!alreadyInitialized) {
            // Stage 1: Register G2P/Driver plugin search paths.
            // addPluginPath scans subdirectories for plugin.json descriptors and is
            // the framework's plugin loading entry point.
            for (const auto &path : pluginSearchPaths) {
                mgr->addPluginPath(srt::g2p::kTaskPluginIid, path);
                mgr->addPluginPath(srt::g2p::kDriverPluginIid, path);
            }

            // Stage 2: Register official G2P package paths to the default context.
            // Default-context failures block Manager::initialize().
            for (const auto &path : officialG2pPackagePaths) {
                if (path.empty()) {
                    continue;
                }
                auto exp = mgr->addPackagePath(srt::g2p::kOfficialContext, path);
                if (!exp) {
                    return srt::core::Error(
                        srt::core::Error::SessionError,
                        "Failed to register official G2P package path: " +
                            exp.error().message());
                }
            }
        }

        // Stage 3: Register voicebank private G2P packages for each singer.
        // Always executed: depends on this instance's packageDirs, not the
        // global singleton state. Non-default-context failures only mark the
        // context Failed and do not block other contexts, so per-package errors
        // are reported via std::cerr (srt::core::Logger not yet migrated) but
        // do not interrupt the loop (ER-03).
        for (const auto &kv : _impl->packageDirs) {
            const auto &packageDir = kv.second;

            ds::bank::PackageParser parser;
            auto packageResult = parser.parsePackage(packageDir);
            if (!packageResult) {
                std::cerr << "[LanguageService] WARNING: failed to parse package for G2P registration: "
                          << packageDir << ": " << packageResult.error().message() << "\n";
                continue;
            }
            auto package = std::move(*packageResult);

            for (const auto &singer : package.singers()) {
                for (const auto &lang : singer.languages()) {
                    auto routeExp = resolveG2pRoute(
                        package, singer.singerId(), lang.languageId());
                    if (!routeExp) {
                        std::cerr << "[LanguageService] WARNING: failed to resolve G2P route for singer "
                                  << singer.singerId() << ", lang " << lang.languageId()
                                  << ": " << routeExp.error().message() << "\n";
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
                                std::cerr << "[LanguageService] WARNING: addPackagePath failed for singer "
                                          << route.singerId << ", path " << g2pPath
                                          << ": " << addExp.error().message() << "\n";
                            }
                        } else {
                            auto addExp = mgr->addPackagePath(route.singerId, version, g2pPath);
                            if (!addExp) {
                                std::cerr << "[LanguageService] WARNING: addPackagePath failed for singer "
                                          << route.singerId << ", version " << route.g2pPackageVersion
                                          << ", path " << g2pPath
                                          << ": " << addExp.error().message() << "\n";
                            }
                        }
                    }
                }
            }
        }

        if (!alreadyInitialized) {
            // Stage 4: Initialize all registered G2P contexts.
            auto g2pResult = mgr->initialize();
            if (!g2pResult) {
                return srt::core::Error(
                    srt::core::Error::SessionError,
                    "G2P Manager initialization failed: " +
                        g2pResult.error().message());
            }
        }

        return {};
    }

    srt::core::Expected<LanguageRoute> LanguageService::resolveLanguageRoute(
        const std::string &packageId,
        const std::string &singerId,
        const std::string &languageId) const {

        const auto it = _impl->packageDirs.find(packageId);
        if (it == _impl->packageDirs.end()) {
            return srt::core::Error(
                srt::core::Error::FileNotFound,
                "package directory not found for packageId: " + packageId);
        }
        const auto &packageDir = it->second;

        ds::bank::PackageParser parser;
        auto packageResult = parser.parsePackage(packageDir);
        if (!packageResult) {
            return packageResult.takeError();
        }
        auto package = std::move(*packageResult);

        const auto singerIt = std::find_if(
            package.singers().begin(), package.singers().end(),
            [&](const ds::bank::SingerManifest &singer) {
                return singer.singerId() == singerId;
            });
        if (singerIt == package.singers().end()) {
            return srt::core::Error(
                srt::core::Error::InvalidArgument,
                "singer not found in package: " + singerId);
        }

        std::string resolvedLanguageId = languageId.empty()
            ? singerIt->defaultLanguage() : languageId;
        if (resolvedLanguageId.empty()) {
            return srt::core::Error(
                srt::core::Error::InvalidArgument,
                "language id is required because singer has no defaultLanguage");
        }
        if (!singerIt->languages().empty() &&
            !containsLanguage(singerIt->languages(), resolvedLanguageId)) {
            return srt::core::Error(
                srt::core::Error::InvalidArgument,
                "language not declared by singer: " + resolvedLanguageId);
        }

        const auto languageIt = std::find_if(
            package.languages().begin(), package.languages().end(),
            [&](const ds::bank::LanguageInfo &language) {
                return language.languageId() == resolvedLanguageId;
            });
        if (languageIt == package.languages().end()) {
            return srt::core::Error(
                srt::core::Error::InvalidArgument,
                "language resource not found in package: " + resolvedLanguageId);
        }

        LanguageRoute result;
        result.g2pId = languageIt->g2pId();
        result.singerId = singerIt->singerId();
        result.g2pContextVersion = languageIt->hasG2pPackageVersion()
            ? languageIt->g2pPackageVersion()
            : stdc::VersionNumber();
        result.voicebankContext = !languageIt->g2pPackages().empty();
        result.s2pMode = languageIt->s2pMode();
        result.s2pFile = !languageIt->s2pFile().empty()
            ? languageIt->s2pFile() : languageIt->dict();
        result.onsetFile = languageIt->onsetFile();
        return result;
    }

    std::vector<srt::g2p::G2pRes> LanguageService::convertLyric(
        const std::vector<srt::g2p::G2pInput> &input) const {
        return srt::g2p::Manager::instance()->convert(input);
    }

    bool LanguageService::convert(const std::string &packageId,
                                  const std::string &singerId,
                                  const std::string &languageId,
                                  const std::vector<srt::g2p::G2pInput> &inputs,
                                  std::vector<srt::g2p::G2pRes> &outputs,
                                  srt::core::Diagnostic *error) const {
        auto routeExp = resolveLanguageRoute(packageId, singerId, languageId);
        if (!routeExp) {
            if (error) {
                *error = routeExp.error().diagnostic();
            }
            return false;
        }
        outputs = convertLyric(inputs);
        for (const auto &res : outputs) {
            if (res.isFailed()) {
                if (error) {
                    error->code = srt::core::ErrorCode::SessionError;
                    error->severity = srt::core::Severity::Error;
                    error->message = "G2P conversion failed (errorType=" +
                                     std::to_string(static_cast<int>(res.errorType)) + ")";
                }
                return false;
            }
        }
        return true;
    }

} // namespace ds::lang
