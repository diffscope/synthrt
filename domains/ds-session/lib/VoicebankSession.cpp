#include <diffsinger/Session/VoicebankSession.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <utility>

#include <stdcorelib/path.h>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Logging.h>
#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/G2P/LanguageService.h>
#include <synthrt/S2P/LanguageResource.h>

#include <diffsinger/Bank/VoicebankScanner.h>
#include <diffsinger/Infer/ModelSet.h>
#include <diffsinger/Infer/SingerStageResolver.h>
#include <diffsinger/Session/ModelSetHandle.h>

namespace ds::session {
namespace {

static srt::core::LogCategory SessionLog("ds.session");

AvailabilityLevel availabilityOf(const ds::bank::SingerSnapshot &singer,
                                 const std::vector<std::string> &reserved) {
    if (singer.resolutionState != ds::bank::ResolutionState::Resolved || singer.inferenceIds.empty())
        return AvailabilityLevel::Unavailable;
    if (!singer.capabilityReport)
        return AvailabilityLevel::Available;
    const auto &report = *singer.capabilityReport;
    if (report.phonemeConsistency == ds::bank::ConsistencyLevel::Inconsistent ||
        report.speakerConsistency == ds::bank::ConsistencyLevel::Inconsistent ||
        report.effectivePhonemes.empty())
        return AvailabilityLevel::Unavailable;
    for (const auto &phoneme : reserved) {
        if (std::find(report.effectivePhonemes.begin(), report.effectivePhonemes.end(), phoneme) ==
            report.effectivePhonemes.end())
            return AvailabilityLevel::Degraded;
    }
    return report.phonemeDegraded ||
                   report.phonemeConsistency == ds::bank::ConsistencyLevel::Degraded ||
                   report.speakerConsistency == ds::bank::ConsistencyLevel::Degraded ||
                   report.languageConsistency == ds::bank::ConsistencyLevel::Degraded
               ? AvailabilityLevel::Degraded : AvailabilityLevel::Available;
}

void addAvailability(AvailabilitySummary &summary, AvailabilityLevel level) {
    if (level == AvailabilityLevel::Available) ++summary.available;
    else if (level == AvailabilityLevel::Degraded) ++summary.degraded;
    else ++summary.unavailable;
}

/// Build a PackageCoordinate from a PackageStatus.
PackageCoordinate coordinateOf(const ds::bank::PackageStatus &pkg) {
    PackageCoordinate c;
    c.packageId = pkg.packageId;
    c.version = pkg.version;
    return c;
}

/// Build a PackageCoordinate from a SingerSnapshot (uses its ref's packageId/version).
PackageCoordinate coordinateOf(const ds::bank::SingerSnapshot &singer) {
    PackageCoordinate c;
    c.packageId = singer.ref.packageId;
    if (!singer.version.empty())
        c.version = stdc::VersionNumber::fromString(singer.version);
    else if (!singer.ref.version.empty())
        c.version = stdc::VersionNumber::fromString(singer.ref.version);
    return c;
}

std::string coordinateKey(const PackageCoordinate &coordinate) {
    return coordinate.packageId + "\n" + coordinate.version.toString();
}

std::string fingerprintSinger(const ds::bank::SingerSnapshot &singer) {
    std::ostringstream stream;
    stream << singer.ref.packageId << '\n' << singer.ref.singerId << '\n'
           << singer.ref.version << '\n' << singer.name << '\n'
           << static_cast<int>(singer.resolutionState) << '\n'
           << singer.phonemeLength << '\n' << singer.defaultLanguage << '\n';
    for (const auto &item : singer.languages) stream << item << '\n';
    stream << '\x1e';
    for (const auto &item : singer.speakerIds) stream << item << '\n';
    stream << '\x1e';
    for (const auto &item : singer.inferenceIds) stream << item << '\n';
    if (singer.capabilityReport) {
        const auto &report = *singer.capabilityReport;
        stream << '\x1e' << static_cast<int>(report.phonemeConsistency)
               << static_cast<int>(report.speakerConsistency)
               << static_cast<int>(report.languageConsistency)
               << report.phonemeDegraded << '\n';
        for (const auto &item : report.effectivePhonemes) stream << item << '\n';
        stream << '\x1e';
        for (const auto &item : report.effectiveLanguages) stream << item << '\n';
        stream << '\x1e';
        for (const auto &item : report.mixableSpeakers) stream << item << '\n';
    }
    return stream.str();
}

std::string fingerprintPackage(const VoicebankSnapshot &snapshot,
                               const ds::bank::PackageStatus &package) {
    std::ostringstream stream;
    stream << package.packageId << '\n' << package.version.toString() << '\n'
           << stdc::path::to_utf8(package.rootPath) << '\n' << package.valid << '\n';
    for (const auto &dependency : package.dependencies) stream << dependency << '\n';
    stream << '\x1e';
    for (const auto &dependency : package.unresolvedDependencies) stream << dependency << '\n';
    stream << '\x1e' << static_cast<int>(package.error.code) << '\n'
           << package.error.message << '\n';
    for (const auto &singer : snapshot.singers) {
        if (coordinateOf(singer) == coordinateOf(package))
            stream << fingerprintSinger(singer) << '\x1f';
    }
    return stream.str();
}

/// Serialize only the language-route-relevant fields of a package (V3-07 D-33
/// languageFingerprint). Excludes manifest/speaker/capability data that does
/// not affect G2P/S2P routing. Paths use stdc::path::to_utf8 for cross-platform
/// stability (V3-15 §3.2).
std::string fingerprintPackageLanguage(const VoicebankSnapshot &snapshot,
                                       const ds::bank::PackageStatus &package) {
    std::ostringstream stream;
    stream << package.packageId << '\n' << package.version.toString() << '\n'
           << stdc::path::to_utf8(package.rootPath) << '\n';
    for (const auto &singer : snapshot.singers) {
        if (coordinateOf(singer) == coordinateOf(package)) {
            stream << singer.ref.singerId << '\n'
                   << singer.ref.packageId << '\n'
                   << singer.ref.version << '\n'
                   << singer.defaultLanguage << '\n';
            for (const auto &lang : singer.languages) stream << lang << '\n';
            stream << '\x1e';
        }
    }
    return stream.str();
}

/// Compute the catalog fingerprint: concatenate fingerprintPackage output for
/// all packages sorted by (packageId, version). Content-stable: identical
/// package sets produce identical fingerprints regardless of scan order.
std::string computeCatalogFingerprint(const VoicebankSnapshot &snapshot) {
    std::vector<const ds::bank::PackageStatus *> sorted;
    sorted.reserve(snapshot.packages.size());
    for (const auto &pkg : snapshot.packages) sorted.push_back(&pkg);
    std::sort(sorted.begin(), sorted.end(),
              [](const ds::bank::PackageStatus *a, const ds::bank::PackageStatus *b) {
                  if (a->packageId != b->packageId)
                      return a->packageId < b->packageId;
                  return a->version.toString() < b->version.toString();
              });
    std::ostringstream stream;
    for (const auto *pkg : sorted)
        stream << fingerprintPackage(snapshot, *pkg) << '\x1f';
    return stream.str();
}

/// Compute the language fingerprint: same sort order as catalog, but only
/// language-route-relevant fields. Used by Lite for inference cache keys
/// (V3-07 §2.4) — any change here invalidates cached G2P/S2P results.
std::string computeLanguageFingerprint(const VoicebankSnapshot &snapshot) {
    std::vector<const ds::bank::PackageStatus *> sorted;
    sorted.reserve(snapshot.packages.size());
    for (const auto &pkg : snapshot.packages) sorted.push_back(&pkg);
    std::sort(sorted.begin(), sorted.end(),
              [](const ds::bank::PackageStatus *a, const ds::bank::PackageStatus *b) {
                  if (a->packageId != b->packageId)
                      return a->packageId < b->packageId;
                  return a->version.toString() < b->version.toString();
              });
    std::ostringstream stream;
    for (const auto *pkg : sorted)
        stream << fingerprintPackageLanguage(snapshot, *pkg) << '\x1f';
    return stream.str();
}

std::map<std::string, std::string> packageFingerprints(const VoicebankSnapshot &snapshot) {
    std::map<std::string, std::string> fingerprints;
    for (const auto &package : snapshot.packages) {
        const auto coordinate = coordinateOf(package);
        fingerprints.emplace(coordinateKey(coordinate), fingerprintPackage(snapshot, package));
    }
    return fingerprints;
}

/// Compare two snapshots by package set and singer availability, filling the
/// supplied ChangeSummary. added/removed/changed describe package deltas;
/// disabled lists packages whose singers transitioned from Resolved to a
/// non-Resolved state or became Unavailable after being Available.
void computeChanges(const VoicebankSnapshot &prev, const VoicebankSnapshot &next,
                    ChangeSummary &out) {
    const auto prevFingerprints = packageFingerprints(prev);
    const auto nextFingerprints = packageFingerprints(next);

    // added: in next but not in prev.
    for (const auto &p : next.packages) {
        const auto coordinate = coordinateOf(p);
        if (prevFingerprints.find(coordinateKey(coordinate)) == prevFingerprints.end())
            out.added.push_back(coordinateOf(p));
    }
    // removed: in prev but not in next.
    for (const auto &p : prev.packages) {
        const auto coordinate = coordinateOf(p);
        if (nextFingerprints.find(coordinateKey(coordinate)) == nextFingerprints.end())
            out.removed.push_back(coordinateOf(p));
    }
    // changed: same coordinate with different visible package or singer data.
    for (const auto &p : next.packages) {
        const auto coordinate = coordinateOf(p);
        const auto key = coordinateKey(coordinate);
        const auto it = prevFingerprints.find(key);
        if (it != prevFingerprints.end() && it->second != nextFingerprints.at(key))
            out.changed.push_back(coordinateOf(p));
    }

    // disabled: singers that were Available/Degraded before but are now
    // Unavailable (e.g. their package was removed or their resolution state
    // degraded). Report at the package coordinate level.
    auto findSingerByExactRef = [](const VoicebankSnapshot &snapshot,
                                   const ds::bank::SingerRef &ref)
        -> const ds::bank::SingerSnapshot * {
        for (const auto &s : snapshot.singers) {
            if (s.ref.packageId == ref.packageId && s.ref.singerId == ref.singerId &&
                s.ref.version == ref.version)
                return &s;
        }
        return nullptr;
    };
    for (const auto &s : next.singers) {
        const auto *prevSinger = findSingerByExactRef(prev, s.ref);
        if (!prevSinger)
            continue;
        const auto prevLevel = availabilityOf(*prevSinger, prev.reservedPhonemes);
        const auto nextLevel = availabilityOf(s, next.reservedPhonemes);
        if (prevLevel != AvailabilityLevel::Unavailable &&
            nextLevel == AvailabilityLevel::Unavailable) {
            out.disabled.push_back(coordinateOf(s));
        }
    }
    for (const auto &s : prev.singers) {
        if (findSingerByExactRef(next, s.ref))
            continue;
        if (availabilityOf(s, prev.reservedPhonemes) != AvailabilityLevel::Unavailable)
            out.disabled.push_back(coordinateOf(s));
    }

    // Deduplicate disabled entries: multiple singers under the same package
    // coordinate (e.g. several singerIds in one package, or a singer that was
    // both degraded and removed during the refresh) can each push the same
    // PackageCoordinate. Collapse to unique coordinates, preserving first-seen
    // order so callers observe a stable list. coordinateKey produces a string
    // key, avoiding the need for operator< on PackageCoordinate.
    {
        std::set<std::string> seen;
        std::vector<PackageCoordinate> unique;
        unique.reserve(out.disabled.size());
        for (const auto &c : out.disabled) {
            if (seen.insert(coordinateKey(c)).second) {
                unique.push_back(c);
            }
        }
        out.disabled = std::move(unique);
    }
}

/// Collect per-package parse diagnostics from the scanned PackageStatus list.
/// Only invalid packages contribute a Diagnostic; valid ones are silent.
void collectDiagnostics(const std::vector<ds::bank::PackageStatus> &packages,
                        std::vector<srt::core::Diagnostic> &out) {
    for (const auto &p : packages) {
        if (p.valid)
            continue;
        srt::core::Diagnostic d = p.error;
        if (d.packageId.empty())
            d.packageId = p.packageId;
        if (d.code == srt::core::ErrorCode::None)
            d.code = srt::core::ErrorCode::PackageManifestInvalid;
        if (d.message.empty())
            d.message = "package '" + p.packageId + "' is invalid";
        out.push_back(std::move(d));
    }
}

/// Compare two snapshots for content equality (the fields Lite would observe).
/// generation is intentionally excluded — content equality determines whether
/// a refresh reports changed=true.
bool contentEqual(const VoicebankSnapshot &a, const VoicebankSnapshot &b) {
    if (a.roots != b.roots) return false;
    if (a.reservedPhonemes != b.reservedPhonemes) return false;
    return packageFingerprints(a) == packageFingerprints(b);
}

/// Map the internal AvailabilityLevel (per-singer availability computed from
/// the capability report) to the Lite-facing Availability enum.
Availability toAvailability(AvailabilityLevel level) {
    switch (level) {
        case AvailabilityLevel::Available:  return Availability::Ready;
        case AvailabilityLevel::Degraded:    return Availability::Warning;
        case AvailabilityLevel::Unavailable: return Availability::Disabled;
    }
    return Availability::Disabled;
}

/// Find a singer snapshot by SingerRef. Matches packageId + singerId; an empty
/// version in the key matches any version (backward compat).
///
/// D-42/K-06: when the key omits version (or packageId) and multiple singers
/// match, returns SvsSingerAmbiguous instead of silently picking the first
/// one. This mirrors SingerStageResolver's ambiguous branch at the snapshot
/// layer, so callers receive a consistent disambiguation signal whether the
/// conflict surfaces at snapshot resolution or Runtime stage resolution.
srt::core::Expected<const ds::bank::SingerSnapshot *>
findSinger(const VoicebankSnapshot &snap, const ds::bank::SingerRef &key) {
    const ds::bank::SingerSnapshot *firstMatch = nullptr;
    int matchCount = 0;
    for (const auto &s : snap.singers) {
        if (s.ref.singerId != key.singerId)
            continue;
        if (!key.packageId.empty() && s.ref.packageId != key.packageId)
            continue;
        if (!key.version.empty() && s.ref.version != key.version)
            continue;
        ++matchCount;
        if (matchCount == 1) {
            firstMatch = &s;
        }
    }
    if (matchCount == 0) {
        return srt::core::Error::inferenceError(
            srt::core::ErrorCode::SvsSingerNotFound,
            "singer not found in snapshot: " + key.toString(),
            key.singerId);
    }
    if (matchCount > 1) {
        // D-32/K-06: multi-version/multi-package ambiguity must be rejected
        // explicitly, not silently resolved to the first match. Mirrors
        // SvsSingerAmbiguous at the SingerStageResolver layer.
        return srt::core::Error::inferenceError(
            srt::core::ErrorCode::SvsSingerAmbiguous,
            "ambiguous singer: " + std::to_string(matchCount) +
                " singers match " + key.toString() +
                "; provide a non-empty version to disambiguate",
            key.singerId);
    }
    return firstMatch;
}

} // namespace

class RefreshSubscription::State {
public:
    State(std::function<void(const RefreshResult &)> callback)
        : callback(std::move(callback)) {}

    std::atomic<bool> active{true};
    std::function<void(const RefreshResult &)> callback;
};

RefreshSubscription::RefreshSubscription(std::shared_ptr<State> state)
    : _state(std::move(state)) {}

RefreshSubscription::~RefreshSubscription() {
    reset();
}

RefreshSubscription::RefreshSubscription(RefreshSubscription &&other) noexcept
    : _state(std::move(other._state)) {}

RefreshSubscription &RefreshSubscription::operator=(RefreshSubscription &&other) noexcept {
    if (this != &other) {
        reset();
        _state = std::move(other._state);
    }
    return *this;
}

void RefreshSubscription::reset() {
    if (_state)
        _state->active.store(false, std::memory_order_release);
    _state.reset();
}

RefreshSubscription::operator bool() const noexcept {
    return _state && _state->active.load(std::memory_order_acquire);
}

class VoicebankSession::Impl {
public:
    mutable std::mutex mutex;
    std::vector<std::filesystem::path> roots;
    std::vector<std::string> reservedPhonemes;
    std::shared_ptr<srt::g2p::LanguageService> languageService;
    srt::core::Runtime *runtime = nullptr;
    std::shared_ptr<const VoicebankSnapshot> current;
    std::shared_future<RefreshResult> inFlight;
    std::vector<std::weak_ptr<RefreshSubscription::State>> refreshSubscriptions;
    unsigned long long generation = 0;
    // S1: G2P plugin paths from SessionResources, used by performRefresh
    // to self-initialize LanguageService without host intervention.
    std::vector<std::filesystem::path> g2pPluginPaths;
    std::vector<std::filesystem::path> officialG2pPackages;
    // S2: Loaded package tracking for explicit load/unload API.
    // Key: packageId + "\n" + version.toString()
    struct LoadedPackageEntry {
        std::filesystem::path normalizedPath;
        unsigned long long loadedGeneration = 0;
        int activeHandleCount = 0;   // Mutex-protected (see createModelSet deleter)
        bool pendingUnload = false;   // Mutex-protected; set by unloadVoicebank, checked by deleter
    };
    std::map<std::string, LoadedPackageEntry> loadedPackages;

    void notifyRefresh(const RefreshResult &result) {
        std::vector<std::shared_ptr<RefreshSubscription::State>> subscribers;
        {
            std::lock_guard lock(mutex);
            auto out = refreshSubscriptions.begin();
            for (auto it = refreshSubscriptions.begin(); it != refreshSubscriptions.end(); ++it) {
                if (const auto subscription = it->lock()) {
                    if (subscription->active.load(std::memory_order_acquire))
                        subscribers.push_back(subscription);
                    *out++ = *it;
                }
            }
            refreshSubscriptions.erase(out, refreshSubscriptions.end());
        }

        for (const auto &subscription : subscribers) {
            if (!subscription->active.load(std::memory_order_acquire))
                continue;
            try {
                subscription->callback(result);
            } catch (const std::exception &e) {
                // BUG-11: 调用方回调抛异常表示其代码有 bug，必须记录而非静默吞没
                // (ROBUST-05)。RefreshResult 已通过 future 可观察，但异常本身
                // 仍需显式记录以便排查。
                SessionLog.srtWarning("VoicebankSession: refresh callback threw: %1",
                                      e.what());
            } catch (...) {
                SessionLog.srtWarning("VoicebankSession: refresh callback threw unknown exception");
            }
        }
    }

    RefreshResult performRefresh() {
        std::vector<std::filesystem::path> refreshRoots;
        std::vector<std::string> refreshReserved;
        unsigned long long nextGeneration;
        std::shared_ptr<const VoicebankSnapshot> previous;
        {
            std::lock_guard lock(mutex);
            refreshRoots = roots;
            refreshReserved = reservedPhonemes;
            nextGeneration = generation + 1;
            previous = current;
        }
        const auto finish = [this](RefreshResult result) {
            if (!result.succeeded || result.changed)
                notifyRefresh(result);
            return result;
        };
        try {
            ds::bank::VoicebankScanner scanner;
            scanner.setSearchPaths(refreshRoots);
            auto packages = scanner.refresh();
            if (!packages.hasValue()) {
                RefreshResult r;
                r.succeeded = false;
                r.coalesced = false;
                r.snapshot = std::move(previous);
                r.errorMessage = packages.errorMessage();
                return finish(std::move(r));
            }
            // Collect parse diagnostics for invalid packages (does not abort
            // the refresh — Lite consumes them as warnings).
            std::vector<srt::core::Diagnostic> diagnostics;
            collectDiagnostics(packages.value(), diagnostics);

            // D-31: Discovery stage partial success. Valid packages are
            // published in the snapshot; invalid packages contribute
            // diagnostics only. The refresh fails only when packages were
            // scanned but none were valid.
            auto next = std::make_shared<VoicebankSnapshot>();
            next->roots = std::move(refreshRoots);
            next->reservedPhonemes = refreshReserved;
            next->packages.clear();
            next->packages.reserve(packages.value().size());
            for (const auto &pkg : packages.value()) {
                if (pkg.valid)
                    next->packages.push_back(pkg);
            }
            if (next->packages.empty() && !packages.value().empty()) {
                RefreshResult r;
                r.succeeded = false;
                r.coalesced = false;
                r.snapshot = std::move(previous);
                r.diagnostics = std::move(diagnostics);
                for (const auto &pkg : packages.value()) {
                    if (!pkg.valid) {
                        r.errorMessage = pkg.error.message;
                        break;
                    }
                }
                return finish(std::move(r));
            }
            next->singers = scanner.singers();
            // TD-01 (D-39 #2): expose full manifests so lite PackageManager
            // can read author/description/license/singer/speaker/language
            // detail directly from the snapshot without a separate catalog.
            // Only valid packages contribute manifests; invalid ones are
            // already represented in next->packages via PackageStatus.error.
            next->manifests = scanner.manifests();
            next->generation = nextGeneration;
            for (const auto &singer : next->singers)
                addAvailability(next->availability, availabilityOf(singer, next->reservedPhonemes));

            // V3-07 D-33: compute stable fingerprints after building the
            // snapshot content. These digests are content-stable (independent
            // of scan order) and used both for change detection and as Lite
            // inference cache keys. Paths are serialized via stdc::path::to_utf8
            // for cross-platform stability (V3-15 §3.2).
            next->catalogFingerprint = computeCatalogFingerprint(*next);
            next->languageFingerprint = computeLanguageFingerprint(*next);

            // Compute whether the snapshot actually changed and assemble the
            // per-package delta relative to the previous snapshot. `changed`
            // reflects content difference, not just generation bump, so that
            // Lite can skip redundant UI work when a refresh produced no
            // observable change. Roots/reservedPhonemes are config-level inputs
            // and must also trigger changed=true when they differ.
            const bool changed = !previous ||
                                 previous->roots != next->roots ||
                                 previous->reservedPhonemes != next->reservedPhonemes ||
                                 previous->catalogFingerprint != next->catalogFingerprint ||
                                 previous->languageFingerprint != next->languageFingerprint;

            ChangeSummary changes;
            if (previous && changed)
                computeChanges(*previous, *next, changes);

            // A no-op refresh must preserve the published snapshot identity and
            // generation. Otherwise every ModelSetHandle becomes stale even
            // though RefreshResult::changed is false.
            if (!changed && previous) {
                RefreshResult r;
                r.succeeded = true;
                r.changed = false;
                r.snapshot = std::move(previous);
                r.diagnostics = std::move(diagnostics);
                return finish(std::move(r));
            }

            // V3-16 WP8-session: update LanguageService metadata before
            // publishing. Uses incremental updateMetadata() when the service
            // is already initialized (hot-reload path), falling back to a full
            // initializeMetadata() on first call or on updateMetadata failure.
            // S1: When SessionResources.g2pPluginPaths is non-empty, pass them
            // so the session can self-initialize G2P without host intervention.
            // When empty (legacy behavior), the host must have already called
            // initializeMetadata. Non-fatal: the snapshot is still published;
            // language errors surface as Warning diagnostics and the caller can
            // retry via ensureLanguageReady().
            std::shared_ptr<srt::g2p::LanguageService> svc;
            std::vector<std::filesystem::path> pluginPaths;
            std::vector<std::filesystem::path> g2pPaths;
            {
                std::lock_guard lock(mutex);
                svc = languageService;
                pluginPaths = g2pPluginPaths;
                g2pPaths = officialG2pPackages;
            }
            bool languageReady = false;
            if (svc) {
                std::vector<srt::g2p::PackageDirectoryEntry> entries;
                entries.reserve(next->packages.size());
                for (const auto &pkg : next->packages) {
                    if (pkg.valid) {
                        entries.push_back({pkg.packageId, pkg.version, pkg.rootPath});
                    }
                }
                srt::core::Expected<void> langExp;
                if (svc->metadataReady()) {
                    // Incremental update (V3-16). Fallback to full init on error.
                    auto diffExp = svc->updateMetadata(pluginPaths, g2pPaths, entries);
                    if (!diffExp) {
                        // updateMetadata failed (e.g. modelsReady already).
                        // Try full init.
                        langExp = svc->initializeMetadata(pluginPaths, g2pPaths, entries);
                    }
                } else {
                    // First-time full init.
                    langExp = svc->initializeMetadata(pluginPaths, g2pPaths, entries);
                }
                if (!langExp) {
                    srt::core::Diagnostic d;
                    d.code = srt::core::ErrorCode::G2pInitializationError;
                    d.severity = srt::core::Severity::Warning;
                    d.message = "LanguageService metadata update failed: " +
                                langExp.error().message();
                    diagnostics.push_back(std::move(d));
                } else {
                    languageReady = true;
                }
                // HB-14: drain addPackagePath failure diagnostics recorded
                // inside initializeMetadata()/updateMetadata() so they surface
                // in RefreshResult.diagnostics for the host.
                auto pending = svc->drainPendingDiagnostics();
                for (auto &pd : pending)
                    diagnostics.push_back(std::move(pd));
            }

            std::shared_ptr<const VoicebankSnapshot> published = next;
            {
                std::lock_guard lock(mutex);
                generation = nextGeneration;
                current = published;
            }
            RefreshResult r;
            r.succeeded = true;
            r.coalesced = false;
            r.changed = changed;
            r.snapshot = std::move(published);
            r.updatesAvailable = changes.changed;
            r.changes = std::move(changes);
            r.diagnostics = std::move(diagnostics);
            r.languageReady = languageReady;
            return finish(std::move(r));
        } catch (const std::exception &e) {
            RefreshResult r;
            r.succeeded = false;
            r.coalesced = false;
            r.snapshot = std::move(previous);
            r.errorMessage = e.what();
            return finish(std::move(r));
        } catch (...) {
            RefreshResult r;
            r.succeeded = false;
            r.coalesced = false;
            r.snapshot = std::move(previous);
            r.errorMessage = "unknown voicebank refresh failure";
            return finish(std::move(r));
        }
    }
};

VoicebankSession::VoicebankSession() : _impl(std::make_shared<Impl>()) {}

VoicebankSession::VoicebankSession(SessionResources resources)
    : _impl(std::make_shared<Impl>()) {
    _impl->runtime = resources.runtime;
    _impl->languageService = std::move(resources.languageService);
    _impl->g2pPluginPaths = std::move(resources.g2pPluginPaths);
    _impl->officialG2pPackages = std::move(resources.officialG2pPackages);
}

VoicebankSession::~VoicebankSession() {
    // BUG-28 / ARCH-05: 析构需等待 in-flight refresh 完成，避免后台任务在
    // 宿主对象销毁后仍访问对外接口而触发 UB。shared_future::wait() 线程安全
    // 且可多次调用；这里不调用 get()，因为其他线程可能已经 get 过。
    // 不在持有 _impl->mutex 的情况下 wait() —— performRefresh() 内会请求
    // 同一把锁，持锁等待会死锁。先在锁内拷贝 shared_future（拷贝线程安全，
    // 共享同一共享状态），释放锁后再 wait()。
    if (!_impl) {
        // Moved-from state: nothing to clean up.
        return;
    }
    std::shared_future<RefreshResult> pending;
    {
        std::lock_guard lock(_impl->mutex);
        pending = _impl->inFlight;
    }
    if (pending.valid()) {
        pending.wait();
    }
}

// Move-only: shared_ptr<Impl> moves cheaply; moved-from session has empty
// _impl and is safe to destruct (guarded above). Required by ds-editor-lite
// SynthrtEngine::initialize() which reassigns m_session after pluginRoot()
// becomes available (B1a).
VoicebankSession::VoicebankSession(VoicebankSession &&) noexcept = default;
VoicebankSession &VoicebankSession::operator=(VoicebankSession &&) noexcept = default;

void VoicebankSession::setRoots(std::vector<std::filesystem::path> roots) {
    std::lock_guard lock(_impl->mutex);
    _impl->roots = std::move(roots);
}

std::vector<std::filesystem::path> VoicebankSession::roots() const {
    std::lock_guard lock(_impl->mutex);
    return _impl->roots;
}

void VoicebankSession::setReservedPhonemes(std::vector<std::string> phonemes) {
    std::lock_guard lock(_impl->mutex);
    _impl->reservedPhonemes = std::move(phonemes);
}

std::vector<std::string> VoicebankSession::reservedPhonemes() const {
    std::lock_guard lock(_impl->mutex);
    return _impl->reservedPhonemes;
}

std::shared_future<RefreshResult> VoicebankSession::refreshAsync() {
    std::lock_guard lock(_impl->mutex);
    if (_impl->inFlight.valid() &&
        _impl->inFlight.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return _impl->inFlight;
    const auto impl = _impl;
    _impl->inFlight = std::async(std::launch::async, [impl] { return impl->performRefresh(); }).share();
    return _impl->inFlight;
}

RefreshResult VoicebankSession::refresh() {
    return refreshAsync().get();
}

RefreshSubscription VoicebankSession::subscribeRefresh(
    std::function<void(const RefreshResult &)> callback) {
    auto state = std::make_shared<RefreshSubscription::State>(std::move(callback));
    std::lock_guard lock(_impl->mutex);
    _impl->refreshSubscriptions.emplace_back(state);
    return RefreshSubscription(std::move(state));
}

std::shared_ptr<const VoicebankSnapshot> VoicebankSession::snapshot() const {
    std::lock_guard lock(_impl->mutex);
    return _impl->current;
}

AvailabilitySummary VoicebankSession::availability() const {
    const auto view = snapshot();
    return view ? view->availability : AvailabilitySummary{};
}

void VoicebankSession::setLanguageService(std::shared_ptr<srt::g2p::LanguageService> service) {
    std::lock_guard lock(_impl->mutex);
    _impl->languageService = std::move(service);
}

std::shared_ptr<srt::g2p::LanguageService> VoicebankSession::languageService() const {
    std::lock_guard lock(_impl->mutex);
    return _impl->languageService;
}

void VoicebankSession::setRuntime(srt::core::Runtime *runtime) {
    std::lock_guard lock(_impl->mutex);
    _impl->runtime = runtime;
}

srt::core::Runtime *VoicebankSession::runtime() const {
    std::lock_guard lock(_impl->mutex);
    return _impl->runtime;
}

SingerCapabilitySummary VoicebankSession::capabilitySummary(const ds::bank::SingerRef &singerKey) const {
    SingerCapabilitySummary summary;
    const auto snap = snapshot();
    if (!snap) {
        summary.availability = Availability::Disabled;
        return summary;
    }
    auto singerExp = findSinger(*snap, singerKey);
    if (!singerExp.hasValue()) {
        summary.availability = Availability::Disabled;
        srt::core::Diagnostic d;
        d.code = singerExp.error().code();
        d.severity = srt::core::Severity::Error;
        d.message = singerExp.error().message();
        d.packageId = singerKey.packageId;
        d.singerId = singerKey.singerId;
        summary.diagnostics.push_back(std::move(d));
        return summary;
    }
    const auto *singer = *singerExp;
    summary.availability = toAvailability(availabilityOf(*singer, snap->reservedPhonemes));
    // Prefer capabilityReport data when present; otherwise fall back to the
    // singer's flat lists (backward compat with singers that have no report).
    if (singer->capabilityReport) {
        const auto &report = *singer->capabilityReport;
        summary.phonemes = report.effectivePhonemes;
        summary.mixableSpeakers = report.mixableSpeakers;
        summary.languages = report.effectiveLanguages;
        for (const auto &w : report.phonemeWarnings)
            summary.diagnostics.push_back(srt::core::Diagnostic{
                srt::core::ErrorCode::None, srt::core::Severity::Warning, w, {},
                singer->ref.packageId, singer->ref.singerId, {}, {}, {}});
        for (const auto &w : report.speakerWarnings)
            summary.diagnostics.push_back(srt::core::Diagnostic{
                srt::core::ErrorCode::None, srt::core::Severity::Warning, w, {},
                singer->ref.packageId, singer->ref.singerId, {}, {}, {}});
        for (const auto &w : report.languageWarnings)
            summary.diagnostics.push_back(srt::core::Diagnostic{
                srt::core::ErrorCode::None, srt::core::Severity::Warning, w, {},
                singer->ref.packageId, singer->ref.singerId, {}, {}, {}});
    } else {
        summary.languages = singer->languages;
        summary.phonemes = snap->reservedPhonemes;
        summary.mixableSpeakers = singer->speakerIds;
    }
    return summary;
}

srt::core::Expected<std::vector<srt::g2p::G2pRes>>
    VoicebankSession::convertG2p(const ds::bank::SingerRef &singerKey,
                                 const std::string &language,
                                 const std::vector<srt::g2p::G2pInput> &inputs) const {
    const auto snap = snapshot();
    if (!snap) {
        return srt::core::Error(srt::core::ErrorCode::SessionError,
                                "VoicebankSession::convertG2p: no snapshot available");
    }
    auto singerExp = findSinger(*snap, singerKey);
    if (!singerExp.hasValue()) {
        return srt::core::Error::inferenceError(
            singerExp.error().code(),
            "VoicebankSession::convertG2p: " + singerExp.error().message(),
            singerKey.singerId);
    }
    const auto svc = languageService();
    if (!svc) {
        return srt::core::Error(srt::core::ErrorCode::G2pNotImplementedError,
                                "VoicebankSession::convertG2p: no LanguageService configured");
    }
    // V3-10: parse version from SingerRef and delegate to the version-aware
    // LanguageService::convert overload. An empty version triggers
    // G2pVersionAmbiguous inside LanguageService::resolveLanguageRoute when
    // multiple versions of the packageId are registered; single-version
    // scenarios route transparently (backward compat). Per-lyric failures are
    // surfaced via G2pRes::isFailed() rather than Expected (R6: don't lose
    // details).
    //
    // B4: wrap in try/catch to mirror convertS2p's boundary. The underlying
    // Manager::convert -> Task::start() chain is not noexcept and may throw
    // (e.g. ONNX runtime exceptions, std::bad_alloc). CODING-02 requires
    // third-party exceptions to be converted to Error at the API boundary
    // instead of crossing into the host (lite / dsinfer-cli / C ABI).
    const auto version = stdc::VersionNumber::fromString(singerKey.version);
    try {
        return svc->convert(singerKey.packageId, version, singerKey.singerId, language, inputs);
    } catch (const std::exception &e) {
        return srt::core::Error(srt::core::ErrorCode::G2pConversionFailed,
                                std::string("VoicebankSession::convertG2p: ") + e.what())
                .withExtraContext({{"packageId", singerKey.packageId},
                                   {"singerId", singerKey.singerId},
                                   {"version", singerKey.version},
                                   {"language", language}});
    } catch (...) {
        return srt::core::Error(srt::core::ErrorCode::G2pConversionFailed,
                                "VoicebankSession::convertG2p: unknown G2P conversion failure")
                .withExtraContext({{"packageId", singerKey.packageId},
                                   {"singerId", singerKey.singerId},
                                   {"version", singerKey.version},
                                   {"language", language}});
    }
}

srt::core::Expected<S2pResult>
    VoicebankSession::convertS2p(const ds::bank::SingerRef &singerKey,
                                 const std::string &language,
                                 const std::string &pronunciation) const {
    const auto snap = snapshot();
    if (!snap) {
        return srt::core::Error(srt::core::ErrorCode::SessionError,
                                "VoicebankSession::convertS2p: no snapshot available");
    }
    auto singerExp = findSinger(*snap, singerKey);
    if (!singerExp.hasValue()) {
        return srt::core::Error::inferenceError(
            singerExp.error().code(),
            "VoicebankSession::convertS2p: " + singerExp.error().message(),
            singerKey.singerId);
    }
    const auto svc = languageService();
    if (!svc) {
        return srt::core::Error(srt::core::ErrorCode::G2pNotImplementedError,
                                "VoicebankSession::convertS2p: no LanguageService configured");
    }
    // V3-10: parse version from SingerRef and delegate to the version-aware
    // LanguageService::resolveS2pResource overload. An empty version triggers
    // G2pVersionAmbiguous inside LanguageService::resolveLanguageRoute when
    // multiple versions of the packageId are registered; single-version
    // scenarios route transparently (backward compat). Mirrors convertG2p's
    // pattern so multi-version same-packageId S2P conversion is routed
    // precisely.
    const auto version = stdc::VersionNumber::fromString(singerKey.version);
    auto resExp = svc->resolveS2pResource(singerKey.packageId, version, singerKey.singerId, language);
    if (!resExp) {
        return resExp.takeError();
    }
    const auto &resource = *resExp;
    try {
        const auto syllable = resource->convert(pronunciation);
        S2pResult out;
        out.phonemes = syllable.phonemes;
        out.onsets = syllable.onsets;
        return out;
    } catch (const std::exception &e) {
        return srt::core::Error(srt::core::ErrorCode::S2pConversionFailed,
                                std::string("VoicebankSession::convertS2p: ") + e.what())
                .withExtraContext({{"packageId", singerKey.packageId},
                                   {"singerId", singerKey.singerId},
                                   {"version", singerKey.version},
                                   {"language", language}});
    } catch (...) {
        return srt::core::Error(srt::core::ErrorCode::S2pConversionFailed,
                                "VoicebankSession::convertS2p: unknown S2P conversion failure")
                .withExtraContext({{"packageId", singerKey.packageId},
                                   {"singerId", singerKey.singerId},
                                   {"version", singerKey.version},
                                   {"language", language}});
    }
}

srt::core::Expected<void>
    VoicebankSession::validatePhonemes(const ds::bank::SingerRef &singerKey,
                                       const std::vector<std::string> &phonemes) const {
    const auto snap = snapshot();
    if (!snap) {
        return srt::core::Error(srt::core::ErrorCode::SessionError,
                                "VoicebankSession::validatePhonemes: no snapshot available");
    }
    auto singerExp = findSinger(*snap, singerKey);
    if (!singerExp.hasValue()) {
        return srt::core::Error::inferenceError(
            singerExp.error().code(),
            "VoicebankSession::validatePhonemes: " + singerExp.error().message(),
            singerKey.singerId);
    }
    const auto *singer = *singerExp;
    if (!singer->capabilityReport) {
        // Without a capability report we cannot prove phoneme support; report
        // a validation error rather than silently accepting (ROBUST-05).
        return srt::core::Error::inferenceError(
            srt::core::ErrorCode::G2pValidationError,
            "VoicebankSession::validatePhonemes: singer '" + singerKey.toString() +
                "' has no capability report; cannot validate phonemes",
            singerKey.singerId);
    }
    const auto &effective = singer->capabilityReport->effectivePhonemes;
    if (effective.empty()) {
        return srt::core::Error::inferenceError(
            srt::core::ErrorCode::G2pValidationError,
            "VoicebankSession::validatePhonemes: singer '" + singerKey.toString() +
                "' has empty effective phonemes; inference blocked",
            singerKey.singerId);
    }
    // Build a set for O(1) lookup. Reserved phonemes are always accepted.
    std::set<std::string> allowed(effective.begin(), effective.end());
    for (const auto &rp : snap->reservedPhonemes)
        allowed.insert(rp);

    std::vector<std::string> missing;
    for (const auto &p : phonemes) {
        if (allowed.find(p) == allowed.end())
            missing.push_back(p);
    }
    if (!missing.empty()) {
        std::string msg = "unsupported phonemes for singer '" + singerKey.toString() +
                          "': ";
        for (size_t i = 0; i < missing.size(); ++i) {
            if (i) msg += ", ";
            msg += missing[i];
        }
        return srt::core::Error::inferenceError(
            srt::core::ErrorCode::G2pValidationError,
            std::move(msg),
            singerKey.singerId);
    }
    return srt::core::Expected<void>();
}

srt::core::Expected<std::shared_ptr<ModelSetHandle>>
    VoicebankSession::createModelSet(const ds::bank::SingerRef &singerKey) {
    const auto snap = snapshot();
    if (!snap) {
        return srt::core::Error(srt::core::ErrorCode::SessionError,
                                "VoicebankSession::createModelSet: no snapshot available");
    }
    auto singerExp = findSinger(*snap, singerKey);
    if (!singerExp.hasValue()) {
        return srt::core::Error::inferenceError(
            singerExp.error().code(),
            "VoicebankSession::createModelSet: " + singerExp.error().message(),
            singerKey.singerId);
    }
    const auto *singer = *singerExp;
    if (singer->resolutionState != ds::bank::ResolutionState::Resolved) {
        return srt::core::Error::inferenceError(
            srt::core::ErrorCode::SvsSingerNotLoaded,
            "VoicebankSession::createModelSet: singer '" + singerKey.toString() +
                "' is not resolved",
            singerKey.singerId);
    }
    // Read Runtime under the lock; generation comes from the immutable
    // snapshot so no lock is needed for it. The handle is bound to
    // snap->generation and reports stale once the session advances past it.
    srt::core::Runtime *rt;
    {
        std::lock_guard lock(_impl->mutex);
        rt = _impl->runtime;
    }
    if (!rt) {
        return srt::core::Error::inferenceError(
            srt::core::ErrorCode::InferenceNotInitialized,
            "VoicebankSession::createModelSet: no Runtime configured; "
            "call setRuntime() first");
    }
    // S3: Auto-fallback — ensure the singer's package is loaded in Runtime.
    // If not explicitly loaded via loadVoicebank(), do it automatically.
    // This lets lite call createModelSet without a separate load step.
    {
        const std::string verStr = !singer->version.empty()
                                       ? singer->version
                                       : singer->ref.version;
        stdc::VersionNumber ver;
        if (!verStr.empty()) {
            ver = stdc::VersionNumber::fromString(verStr);
        }
        auto loadExp = loadVoicebank(singer->ref.packageId, ver);
        if (!loadExp) {
            auto err = loadExp.takeError();
            err.withContext(singerKey.singerId, {}, singer->ref.packageId, {});
            err.withExtraContext({{"stage", "createModelSet"}});
            return std::move(err);
        }
        // AlreadyLoaded or NewlyLoaded — both are fine
    }
    // Resolve the 5 inference stages from the singer's package in the Runtime.
    // The Runtime must have the singer's package loaded via loadPackage().
    ds::infer::SingerStageResolver resolver;
    const std::string version = !singer->version.empty() ? singer->version
                                                          : singer->ref.version;
    auto stageExp = resolver.resolve(*rt, singer->ref.packageId,
                                     singer->ref.singerId, version);
    if (!stageExp) {
        return std::move(stageExp.takeError());
    }
    auto modelSet = std::make_shared<ds::infer::ModelSet>(std::move(stageExp.take()));
    // Build the staleness callback. It captures a weak_ptr to the session Impl
    // so it can safely outlive the session: when the session is destroyed the
    // weak_ptr expires and the callback returns false, making the handle
    // stale. The generation comparison happens under the session mutex to
    // avoid races with concurrent refresh().
    const auto gen = snap->generation;
    auto implWeak = std::weak_ptr<Impl>(_impl);
    auto isCurrentGen = [implWeak, gen]() -> bool {
        auto sp = implWeak.lock();
        if (!sp) return false;  // session gone → not current → stale
        std::lock_guard lock(sp->mutex);
        return sp->generation == gen;
    };
    // S4: Reference counting — increment activeHandleCount for the package.
    // The custom deleter decrements it and triggers deferred unload if
    // pendingUnload is true and count reaches zero.
    // Key must match loadVoicebank's format: packageId + "\n" + VersionNumber::toString()
    stdc::VersionNumber pkgVer;
    if (!version.empty()) {
        pkgVer = stdc::VersionNumber::fromString(version);
    }
    const auto pkgKey = singer->ref.packageId + "\n" + pkgVer.toString();
    {
        std::lock_guard lock(_impl->mutex);
        auto it = _impl->loadedPackages.find(pkgKey);
        if (it != _impl->loadedPackages.end()) {
            ++it->second.activeHandleCount;
        }
    }
    auto handle = std::shared_ptr<ModelSetHandle>(
        new ModelSetHandle(std::move(modelSet), gen, std::move(isCurrentGen)),
        [implWeak, pkgKey](ModelSetHandle *h) {
            delete h;
            // S4: Decrement reference count and check pendingUnload
            auto sp = implWeak.lock();
            if (!sp) return;  // session gone
            std::filesystem::path pathToUnload;
            bool shouldUnload = false;
            {
                std::lock_guard lock(sp->mutex);
                auto it = sp->loadedPackages.find(pkgKey);
                if (it != sp->loadedPackages.end()) {
                    --it->second.activeHandleCount;
                    if (it->second.pendingUnload &&
                        it->second.activeHandleCount <= 0) {
                        pathToUnload = it->second.normalizedPath;
                        shouldUnload = true;
                        sp->loadedPackages.erase(it);
                    }
                }
            }
            // Unload outside the mutex to avoid blocking other callers
            if (shouldUnload && sp->runtime) {
                auto unloadExp = sp->runtime->unloadPackage(pathToUnload);
                if (!unloadExp) {
                    // Deleter runs during handle destruction; cannot propagate
                    // error to caller, but record it for diagnostics (ROBUST-05).
                    SessionLog.srtWarning(
                        "VoicebankSession: unloadPackage failed for %1: %2",
                        stdc::path::to_utf8(pathToUnload),
                        unloadExp.errorMessage());
                }
            }
        });
    return handle;
}

srt::core::Expected<std::shared_ptr<ModelSetHandle>>
    VoicebankSession::ensureModelSet(const ds::bank::SingerRef &singerKey) {
    // Thin wrapper over createModelSet with explicit error categorization.
    // createModelSet already returns SvsSingerNotFound / InferenceNotInitialized
    // / RuntimePackageNotLoaded / SvsStageResolveFailed as appropriate.
    return createModelSet(singerKey);
}

srt::core::Expected<void> VoicebankSession::ensureLanguageReady(
    const std::string &packageId,
    const stdc::VersionNumber &version,
    const std::string &language) {
    const auto snap = snapshot();
    if (!snap) {
        return srt::core::Error(srt::core::ErrorCode::SessionError,
                                "ensureLanguageReady: no snapshot available");
    }
    // 1. Check (packageId, version) exists in snapshot. Empty version matches
    //    any version (backward compat); non-empty version must match exactly.
    bool found = false;
    int matchCount = 0;
    for (const auto &pkg : snap->packages) {
        if (pkg.packageId == packageId) {
            ++matchCount;
            if (version.isEmpty() || pkg.version == version) {
                found = true;
            }
        }
    }
    // V3-10: caller omitted version while packageId has multiple versions
    // registered — cannot pick one unambiguously. Checked before the
    // single-match fallback so that two same-packageId entries don't get
    // silently resolved to whichever one was iterated first.
    if (version.isEmpty() && matchCount > 1) {
        return srt::core::Error::g2pError(
            srt::core::ErrorCode::G2pVersionAmbiguous,
            "ensureLanguageReady: packageId=" + packageId +
                " has multiple versions; provide a version to disambiguate",
            language, packageId);
    }
    if (!found) {
        return srt::core::Error::g2pError(
            srt::core::ErrorCode::G2pPackageNotFound,
            "ensureLanguageReady: package not found: packageId=" + packageId +
                ", version=" + version.toString(),
            language, packageId);
    }
    // 2. Check Runtime is configured (needed for ensureModelSet downstream,
    //    and conceptually part of "language readiness" for inference chains).
    srt::core::Runtime *rt = runtime();
    if (!rt) {
        return srt::core::Error::inferenceError(
            srt::core::ErrorCode::RuntimePackageNotLoaded,
            "ensureLanguageReady: no Runtime configured; call setRuntime() or "
            "use VoicebankSession(SessionResources) constructor",
            {});
    }
    // 3. Ensure LanguageService is initialized (lazy on first call).
    auto svc = languageService();
    if (!svc) {
        return srt::core::Error(srt::core::ErrorCode::G2pNotImplementedError,
                                "ensureLanguageReady: no LanguageService configured");
    }
    if (!svc->metadataReady()) {
        return srt::core::Error::g2pError(
            srt::core::ErrorCode::G2pInitializationError,
            "ensureLanguageReady: LanguageService metadata not initialized; "
            "call initializeMetadata() first",
            language, packageId);
    }
    if (!svc->modelsReady()) {
        // B5: wrap initializeModels in try/catch to mirror convertG2p/convertS2p
        // boundaries. The underlying Manager::initialize -> loadTasksForCategory
        // chain is not noexcept and may throw (e.g. plugin dlopen failures, ONNX
        // session creation, std::bad_alloc). CODING-02 requires third-party
        // exceptions to be converted to Error at the API boundary.
        try {
            auto exp = svc->initializeModels();
            if (!exp) {
                return srt::core::Error::inferenceError(
                    srt::core::ErrorCode::LoadFailed,
                    "ensureLanguageReady: LanguageService::initializeModels failed: " +
                        exp.error().message(),
                    {});
            }
        } catch (const std::exception &e) {
            return srt::core::Error::g2pError(
                srt::core::ErrorCode::G2pInitializationError,
                std::string("ensureLanguageReady: initializeModels threw: ") + e.what(),
                language, packageId);
        } catch (...) {
            return srt::core::Error::g2pError(
                srt::core::ErrorCode::G2pInitializationError,
                "ensureLanguageReady: initializeModels threw unknown exception",
                language, packageId);
        }
    }
    return {};
}

// === S2: Explicit package load/unload API ===

srt::core::Expected<LoadResult>
    VoicebankSession::loadVoicebank(const std::string &packageId,
                                    const stdc::VersionNumber &version) {
    const auto snap = snapshot();
    if (!snap) {
        return srt::core::Error(srt::core::ErrorCode::SessionError,
                                "loadVoicebank: no snapshot available");
    }
    // 1. Find package in snapshot
    std::filesystem::path rootPath;
    bool found = false;
    for (const auto &pkg : snap->packages) {
        if (pkg.packageId == packageId &&
            (version.isEmpty() || pkg.version == version) && pkg.valid) {
            rootPath = pkg.rootPath;
            found = true;
            break;
        }
    }
    if (!found) {
        return srt::core::Error::packageError(
            srt::core::ErrorCode::RuntimePackageNotLoaded,
            "loadVoicebank: package not found in snapshot: packageId=" + packageId,
            packageId);
    }
    // 2. Normalize path
    std::error_code ec;
    auto normalized = std::filesystem::weakly_canonical(rootPath, ec);
    if (ec) {
        normalized = rootPath;
    }
    const auto key = packageId + "\n" + version.toString();
    // 3. Check duplicate
    srt::core::Runtime *rt = nullptr;
    unsigned long long gen = 0;
    {
        std::lock_guard lock(_impl->mutex);
        if (_impl->loadedPackages.count(key)) {
            return LoadResult::AlreadyLoaded;
        }
        rt = _impl->runtime;
        gen = _impl->generation;
    }
    if (!rt) {
        return srt::core::Error::inferenceError(
            srt::core::ErrorCode::InferenceNotInitialized,
            "loadVoicebank: no Runtime configured");
    }
    // 4. Runtime::loadPackage
    auto loadExp = rt->loadPackage(normalized);
    if (!loadExp) {
        auto err = loadExp.takeError();
        err.withExtraContext({{"stage", "loadPackage"},
                              {"packagePath", stdc::path::to_utf8(normalized)}});
        return std::move(err);
    }
    // 5. Register in loadedPackages
    {
        std::lock_guard lock(_impl->mutex);
        Impl::LoadedPackageEntry entry;
        entry.normalizedPath = normalized;
        entry.loadedGeneration = gen;
        _impl->loadedPackages[key] = std::move(entry);
    }
    return LoadResult::NewlyLoaded;
}

srt::core::Expected<void>
    VoicebankSession::unloadVoicebank(const std::string &packageId,
                                      const stdc::VersionNumber &version) {
    const auto key = packageId + "\n" + version.toString();
    srt::core::Runtime *rt = nullptr;
    std::filesystem::path normalizedPath;
    {
        std::lock_guard lock(_impl->mutex);
        auto it = _impl->loadedPackages.find(key);
        if (it == _impl->loadedPackages.end()) {
            return srt::core::Error::packageError(
                srt::core::ErrorCode::RuntimePackageNotLoaded,
                "unloadVoicebank: package not loaded: packageId=" + packageId,
                packageId);
        }
        // Check active references (S4 will make this meaningful)
        if (it->second.activeHandleCount > 0) {
            it->second.pendingUnload = true;
            return srt::core::Error::packageError(
                srt::core::ErrorCode::PackageInUse,
                "unloadVoicebank: package has active handles: packageId=" + packageId +
                    ", count=" + std::to_string(it->second.activeHandleCount),
                packageId);
        }
        rt = _impl->runtime;
        normalizedPath = it->second.normalizedPath;
        _impl->loadedPackages.erase(it);
    }
    if (rt) {
        auto exp = rt->unloadPackage(normalizedPath);
        if (!exp) {
            return std::move(exp.takeError());
        }
    }
    return {};
}

srt::core::Expected<void>
    VoicebankSession::unloadVoicebank(const std::string &packageId,
                                      const stdc::VersionNumber &version,
                                      ForceUnloadTag) {
    const auto key = packageId + "\n" + version.toString();
    srt::core::Runtime *rt = nullptr;
    std::filesystem::path normalizedPath;
    {
        std::lock_guard lock(_impl->mutex);
        auto it = _impl->loadedPackages.find(key);
        if (it == _impl->loadedPackages.end()) {
            return srt::core::Error::packageError(
                srt::core::ErrorCode::RuntimePackageNotLoaded,
                "unloadVoicebank: package not loaded: packageId=" + packageId,
                packageId);
        }
        // Force unload: skip reference count check
        rt = _impl->runtime;
        normalizedPath = it->second.normalizedPath;
        _impl->loadedPackages.erase(it);
    }
    if (rt) {
        auto exp = rt->unloadPackage(normalizedPath);
        if (!exp) {
            return std::move(exp.takeError());
        }
    }
    return {};
}

std::vector<LoadedVoicebankInfo> VoicebankSession::loadedVoicebanks() const {
    std::vector<LoadedVoicebankInfo> result;
    std::lock_guard lock(_impl->mutex);
    result.reserve(_impl->loadedPackages.size());
    for (const auto &[key, entry] : _impl->loadedPackages) {
        LoadedVoicebankInfo info;
        // Parse key back to packageId and version
        const auto sep = key.find('\n');
        if (sep != std::string::npos) {
            info.packageId = key.substr(0, sep);
            info.version = stdc::VersionNumber::fromString(key.substr(sep + 1));
        }
        info.packagePath = entry.normalizedPath;
        info.loadedGeneration = entry.loadedGeneration;
        info.activeHandleCount = entry.activeHandleCount;
        info.pendingUnload = entry.pendingUnload;
        result.push_back(std::move(info));
    }
    return result;
}

// === A2: VoicebankSnapshot const query methods ===
// Lets hosts (ds-editor-lite) reuse the snapshot's own lookups instead of
// re-implementing O(n) traversals at each call site. The snapshot is immutable
// after publication, so these const methods are safe to call concurrently
// (A2-T11). Pointers are non-owning and valid only while the snapshot is alive.

const ds::bank::SingerSnapshot *
VoicebankSnapshot::findSinger(const ds::bank::SingerRef &ref) const {
    for (const auto &s : singers) {
        if (s.ref.packageId != ref.packageId)
            continue;
        if (s.ref.singerId != ref.singerId)
            continue;
        // Fast path: exact string equality covers both-empty and the common
        // case where versions are already stored as VersionNumber::toString().
        if (s.ref.version == ref.version)
            return &s;
        // Normalized path: "1.0" must match "1.0.0" (A2-T02). Empty version
        // matches only empty version, so skip normalization when either side
        // is empty (avoids treating "" as "0.0.0").
        if (s.ref.version.empty() || ref.version.empty())
            continue;
        if (stdc::VersionNumber::fromString(s.ref.version) ==
            stdc::VersionNumber::fromString(ref.version)) {
            return &s;
        }
    }
    return nullptr;
}

std::vector<const ds::bank::SingerSnapshot *>
VoicebankSnapshot::findSingersBySingerId(const std::string &singerId) const {
    std::vector<const ds::bank::SingerSnapshot *> result;
    for (const auto &s : singers) {
        if (s.ref.singerId == singerId)
            result.push_back(&s);
    }
    return result;
}

const ds::bank::PackageStatus *
VoicebankSnapshot::findPackage(const std::string &packageId,
                               const stdc::VersionNumber &version) const {
    for (const auto &p : packages) {
        // PackageStatus.version is already a stdc::VersionNumber, so its
        // operator== applies normalization (e.g. "1.0" == "1.0.0") for free.
        if (p.packageId == packageId && p.version == version)
            return &p;
    }
    return nullptr;
}

const ds::bank::PackageManifest *
VoicebankSnapshot::findManifest(const std::string &packageId,
                                const stdc::VersionNumber &version) const {
    for (const auto &m : manifests) {
        // PackageManifest exposes version() as a stdc::VersionNumber by value;
        // operator== normalizes. Invalid packages have no manifest entry, so
        // they naturally return nullptr here (TD-01, A2-T10).
        if (m.packageId() == packageId && m.version() == version)
            return &m;
    }
    return nullptr;
}

} // namespace ds::session
