// VoicebankScanner.cpp - Scan voicebank directories and build SingerSnapshot list.
//
// Extracted from DiffSingerSession::refreshPackages() as part of v1 Phase 3 P1-a.
// Scans search paths for directories containing desc.json, parses each with
// PackageParser, and builds SingerSnapshot + PackageStatus lists.

#include <diffsinger/Bank/VoicebankScanner.h>

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/Logging.h>

#include <stdcorelib/str.h>
#include <stdcorelib/support/versionnumber.h>

#include <diffsinger/Bank/PackageManifest.h>
#include <diffsinger/Bank/PackageParser.h>
#include <diffsinger/Bank/SingerManifest.h>

#include "SingerCapabilityAnalyzer.h"

namespace ds::bank {

    static srt::LogCategory Log("ds.bank");

    // Semantic version match: treats "1.0" == "1.0.0" == "1.0.0.0".
    // VersionNumber::toString() normalizes away trailing zeros (e.g. "1.0.0"
    // stored as VersionNumber(1,0,0,0) serializes back to "1.0"), so a plain
    // string comparison would wrongly reject "1.0.0" when the stored value is
    // "1.0". Falls back to string comparison when either side cannot be parsed
    // as a VersionNumber (e.g. pre-release suffixes).
    static bool versionsMatch(const std::string &stored, const std::string &query) {
        if (query.empty()) {
            return true;
        }
        if (stored == query) {
            return true;
        }
        const auto storedVN = stdc::VersionNumber::fromString(stored);
        const auto queryVN = stdc::VersionNumber::fromString(query);
        return storedVN == queryVN;
    }

    class VoicebankScanner::Impl {
    public:
        std::vector<std::filesystem::path> searchPaths;
        std::vector<SingerSnapshot> snapshots;
        std::unordered_map<std::string, std::filesystem::path> packageDirs;

        const SingerSnapshot *findSnapshot(const SingerRef &ref) const {
            for (const auto &s : snapshots) {
                if (s.ref.packageId == ref.packageId &&
                    s.ref.singerId == ref.singerId &&
                    versionsMatch(s.ref.version, ref.version))
                    return &s;
            }
            return nullptr;
        }
    };

    VoicebankScanner::VoicebankScanner()
        : _impl(std::make_unique<Impl>()) {}

    VoicebankScanner::~VoicebankScanner() = default;

    void VoicebankScanner::setSearchPaths(
        const std::vector<std::filesystem::path> &paths) {
        _impl->searchPaths = paths;
    }

    void VoicebankScanner::clear() {
        _impl->searchPaths.clear();
        _impl->snapshots.clear();
        _impl->packageDirs.clear();
    }

    srt::core::Expected<std::vector<PackageStatus>> VoicebankScanner::refresh() {
        _impl->snapshots.clear();
        _impl->packageDirs.clear();

        std::vector<PackageStatus> statuses;

        // Helper: parse a single package directory and populate statuses/snapshots.
        auto parsePackageDir = [this, &statuses](const std::filesystem::path &pkgPath) {
            PackageParser parser;
            auto packageResult = parser.parsePackage(pkgPath);
            if (!packageResult) {
                PackageStatus status;
                status.rootPath = pkgPath;
                status.valid = false;
                // B-09: propagate the inner Error with a VoicebankScanner trace
                // frame before extracting the Diagnostic for PackageStatus.
                auto err = packageResult.takeError();
                err.withTrace(std::source_location::current(), "VoicebankScanner::refresh");
                status.error = err.diagnostic();
                statuses.push_back(std::move(status));
                return;
            }

            auto package = std::move(*packageResult);
            const auto packageId = package.packageId();
            const auto versionStr = package.version().toString();

            // Store packageId -> packageDir so callers can open packages
            // in Runtime and build the packageDirs map for LanguageService.
            _impl->packageDirs[packageId] = pkgPath;

            for (const auto &singer : package.singers()) {
                SingerSnapshot snapshot;
                snapshot.ref.packageId = packageId;
                snapshot.ref.singerId = singer.singerId();
                snapshot.ref.version = versionStr;
                snapshot.version = versionStr;
                snapshot.name = singer.name();
                snapshot.resolutionState = ResolutionState::Resolved;
                snapshot.defaultLanguage = singer.defaultLanguage();
                snapshot.phonemeLength = singer.phonemeLength();

                for (const auto &lang : singer.languages()) {
                    snapshot.languages.push_back(lang.languageId());  // backward compat
                    snapshot.languageInfos.push_back(lang);           // R1: full object
                }

                for (const auto &spk : singer.speakers()) {
                    snapshot.speakerIds.push_back(spk.speakerId());   // backward compat
                    snapshot.speakerInfos.push_back(spk);              // R1: full object
                }

                for (const auto &inf : package.inferences()) {
                    snapshot.inferenceIds.push_back(inf.id);
                }

                // Parse-time capability analysis: mixable speakers / phonemes /
                // languages across non-vocoder stages (03-dsbank-capability.md).
                // Produces nullopt for pure G2P packages (no non-vocoder inference).
                snapshot.capabilityReport = SingerCapabilityAnalyzer::analyze(
                    singer.imports(), package.inferences());
                if (snapshot.capabilityReport) {
                    const auto &r = *snapshot.capabilityReport;
                    // One-shot degradation summary per user preference: only log
                    // once per singer here; lite hosts surface warnings in the
                    // voicebank manager UI.
                    if (r.phonemeConsistency == ConsistencyLevel::Degraded) {
                        std::string missing;
                        for (size_t i = 0; i < r.phonemeWarnings.size(); ++i) {
                            if (i != 0) missing += "; ";
                            missing += r.phonemeWarnings[i];
                        }
                        Log.srtInfo("singer '%1' phoneme degraded: %2",
                                    snapshot.name, missing);
                    }
                    if (r.speakerConsistency == ConsistencyLevel::Inconsistent) {
                        Log.srtInfo("singer '%1' speakers inconsistent: no mixable speakers",
                                    snapshot.name);
                    } else if (r.speakerConsistency == ConsistencyLevel::Degraded) {
                        Log.srtInfo("singer '%1' speakers degraded: %2 mixable",
                                    snapshot.name, r.mixableSpeakers.size());
                    }
                }

                _impl->snapshots.push_back(std::move(snapshot));
            }

            PackageStatus status;
            status.packageId = packageId;
            status.rootPath = pkgPath;
            status.version = package.version();
            status.dependencies = package.dependencies();
            status.valid = true;
            statuses.push_back(std::move(status));
        };

        for (const auto &searchPath : _impl->searchPaths) {
            if (searchPath.empty()) {
                continue;
            }

            std::error_code ec;
            if (!std::filesystem::is_directory(searchPath, ec)) {
                continue;
            }

            // A search path may itself be a package (has desc.json directly).
            // This supports the CLI pattern of passing a single voicebank
            // directory rather than a parent containing multiple voicebanks.
            const auto directDesc = searchPath / "desc.json";
            if (std::filesystem::exists(directDesc, ec)) {
                parsePackageDir(searchPath);
            }

            // Also scan subdirectories for packages.
            try {
                for (auto it = std::filesystem::directory_iterator(searchPath, ec);
                     it != std::filesystem::directory_iterator(); ++it) {
                    const auto &entry = *it;
                    if (!entry.is_directory()) {
                        continue;
                    }

                    const auto pkgPath = entry.path();
                    const auto descPath = pkgPath / "desc.json";
                    if (!std::filesystem::exists(descPath, ec)) {
                        continue;
                    }

                    parsePackageDir(pkgPath);
                }
            } catch (const std::filesystem::filesystem_error &e) {
                PackageStatus status;
                status.rootPath = searchPath;
                status.valid = false;
                status.error.code = srt::core::ErrorCode::PackageRootInvalid;
                status.error.severity = srt::core::Severity::Warning;
                status.error.message =
                    "directory iteration failed: " + std::string(e.what());
                statuses.push_back(std::move(status));
            }
        }

        return statuses;
    }

    const std::vector<SingerSnapshot> &VoicebankScanner::singers() const {
        return _impl->snapshots;
    }

    srt::core::Expected<SingerSnapshot> VoicebankScanner::singerSnapshot(
        const SingerRef &ref) const {
        const auto *snap = _impl->findSnapshot(ref);
        if (!snap) {
            return srt::core::Error::packageError(
                srt::core::ErrorCode::FileNotFound,
                "singer not found: " + ref.toString(),
                ref.packageId);
        }
        return *snap;
    }

    srt::core::Expected<SingerRef> VoicebankScanner::findSinger(
        const std::string &singerId) const {
        return findSinger(singerId, {}, {});
    }

    srt::core::Expected<SingerRef> VoicebankScanner::findSinger(
        const std::string &singerId,
        const std::string &packageId,
        const std::string &version) const {
        for (const auto &snap : _impl->snapshots) {
            if (snap.ref.singerId != singerId)
                continue;
            if (!packageId.empty() && snap.ref.packageId != packageId)
                continue;
            if (!versionsMatch(snap.ref.version, version))
                continue;
            return snap.ref;
        }
        std::string msg = "singer not found: singerId=" + singerId;
        if (!packageId.empty()) {
            msg += ", packageId=" + packageId;
        }
        if (!version.empty()) {
            msg += ", version=" + version;
        }
        return srt::core::Error::packageError(
            srt::core::ErrorCode::FileNotFound, std::move(msg), packageId);
    }

    std::filesystem::path VoicebankScanner::packageDirectory(
        const std::string &packageId) const {
        const auto it = _impl->packageDirs.find(packageId);
        if (it == _impl->packageDirs.end()) {
            return {};
        }
        return it->second;
    }

} // namespace ds::bank
