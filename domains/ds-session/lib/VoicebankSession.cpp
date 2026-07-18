#include <diffsinger/Session/VoicebankSession.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <utility>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/G2P/LanguageService.h>
#include <synthrt/S2P/LanguageResource.h>

#include <diffsinger/Bank/VoicebankScanner.h>
#include <diffsinger/Infer/ModelSet.h>
#include <diffsinger/Infer/SingerStageResolver.h>
#include <diffsinger/Session/ModelSetHandle.h>

namespace ds::session {
namespace {

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

/// Compare two snapshots by package set and singer availability, filling the
/// supplied ChangeSummary. added/removed/changed describe package deltas;
/// disabled lists packages whose singers transitioned from Resolved to a
/// non-Resolved state or became Unavailable after being Available.
void computeChanges(const VoicebankSnapshot &prev, const VoicebankSnapshot &next,
                    ChangeSummary &out) {
    // Index previous packages by (packageId, version).
    std::map<std::string, stdc::VersionNumber> prevPkgs;
    for (const auto &p : prev.packages)
        prevPkgs.emplace(p.packageId, p.version);
    std::map<std::string, stdc::VersionNumber> nextPkgs;
    for (const auto &p : next.packages)
        nextPkgs.emplace(p.packageId, p.version);

    // added: in next but not in prev.
    for (const auto &p : next.packages) {
        if (prevPkgs.find(p.packageId) == prevPkgs.end())
            out.added.push_back(coordinateOf(p));
    }
    // removed: in prev but not in next.
    for (const auto &p : prev.packages) {
        if (nextPkgs.find(p.packageId) == nextPkgs.end())
            out.removed.push_back(coordinateOf(p));
    }
    // changed: same packageId present in both but with a different version.
    for (const auto &p : next.packages) {
        const auto it = prevPkgs.find(p.packageId);
        if (it != prevPkgs.end() && !(it->second == p.version))
            out.changed.push_back(coordinateOf(p));
    }

    // disabled: singers that were Available/Degraded before but are now
    // Unavailable (e.g. their package was removed or their resolution state
    // degraded). Report at the package coordinate level.
    auto findPrevSinger = [&](const ds::bank::SingerRef &ref)
        -> const ds::bank::SingerSnapshot * {
        for (const auto &s : prev.singers) {
            if (s.ref.packageId == ref.packageId && s.ref.singerId == ref.singerId)
                return &s;
        }
        return nullptr;
    };
    for (const auto &s : next.singers) {
        const auto *prevSinger = findPrevSinger(s.ref);
        if (!prevSinger)
            continue;
        const auto prevLevel = availabilityOf(*prevSinger, prev.reservedPhonemes);
        const auto nextLevel = availabilityOf(s, next.reservedPhonemes);
        if (prevLevel != AvailabilityLevel::Unavailable &&
            nextLevel == AvailabilityLevel::Unavailable) {
            out.disabled.push_back(coordinateOf(s));
        }
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
    if (a.packages.size() != b.packages.size()) return false;
    for (size_t i = 0; i < a.packages.size(); ++i) {
        if (a.packages[i].packageId != b.packages[i].packageId) return false;
        if (!(a.packages[i].version == b.packages[i].version)) return false;
        if (a.packages[i].valid != b.packages[i].valid) return false;
    }
    if (a.singers.size() != b.singers.size()) return false;
    for (size_t i = 0; i < a.singers.size(); ++i) {
        const auto &x = a.singers[i];
        const auto &y = b.singers[i];
        if (x.ref.packageId != y.ref.packageId) return false;
        if (x.ref.singerId != y.ref.singerId) return false;
        if (x.ref.version != y.ref.version) return false;
        if (x.resolutionState != y.resolutionState) return false;
        if (x.inferenceIds != y.inferenceIds) return false;
    }
    return true;
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
/// version in the key matches any version (backward compat). Returns nullptr
/// when no singer matches.
const ds::bank::SingerSnapshot *findSinger(const VoicebankSnapshot &snap,
                                           const ds::bank::SingerRef &key) {
    for (const auto &s : snap.singers) {
        if (s.ref.singerId != key.singerId)
            continue;
        if (!key.packageId.empty() && s.ref.packageId != key.packageId)
            continue;
        if (!key.version.empty() && s.ref.version != key.version)
            continue;
        return &s;
    }
    return nullptr;
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
            } catch (...) {
                // Refresh completion remains observable through the future even
                // when a client callback throws.
            }
        }
    }

    RefreshResult refresh() {
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

            // Per the contract, a single invalid package aborts the refresh so
            // that Lite never sees a half-built snapshot. The diagnostics
            // collected above are still returned to the caller.
            for (const auto &package : packages.value()) {
                if (!package.valid) {
                    RefreshResult r;
                    r.succeeded = false;
                    r.coalesced = false;
                    r.snapshot = std::move(previous);
                    r.diagnostics = std::move(diagnostics);
                    r.errorMessage = package.error.message;
                    return finish(std::move(r));
                }
            }

            auto next = std::make_shared<VoicebankSnapshot>();
            next->roots = std::move(refreshRoots);
            next->reservedPhonemes = refreshReserved;
            next->packages = packages.value();
            next->singers = scanner.singers();
            next->generation = nextGeneration;
            for (const auto &singer : next->singers)
                addAvailability(next->availability, availabilityOf(singer, next->reservedPhonemes));

            // Compute whether the snapshot actually changed and assemble the
            // per-package delta relative to the previous snapshot. `changed`
            // reflects content difference, not just generation bump, so that
            // Lite can skip redundant UI work when a refresh produced no
            // observable change.
            const bool changed = !previous || !contentEqual(*previous, *next);

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
            r.changes = std::move(changes);
            r.diagnostics = std::move(diagnostics);
            // updatesAvailable intentionally left empty (future capability).
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
VoicebankSession::~VoicebankSession() = default;

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
    _impl->inFlight = std::async(std::launch::async, [impl] { return impl->refresh(); }).share();
    return _impl->inFlight;
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
    const auto *singer = findSinger(*snap, singerKey);
    if (!singer) {
        summary.availability = Availability::Disabled;
        srt::core::Diagnostic d;
        d.code = srt::core::ErrorCode::SvsSingerNotFound;
        d.severity = srt::core::Severity::Error;
        d.message = "singer not found in snapshot: " + singerKey.toString();
        d.packageId = singerKey.packageId;
        d.singerId = singerKey.singerId;
        summary.diagnostics.push_back(std::move(d));
        return summary;
    }
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
    if (!findSinger(*snap, singerKey)) {
        return srt::core::Error::inferenceError(
            srt::core::ErrorCode::SvsSingerNotFound,
            "VoicebankSession::convertG2p: singer not found: " + singerKey.toString(),
            singerKey.singerId);
    }
    const auto svc = languageService();
    if (!svc) {
        return srt::core::Error(srt::core::ErrorCode::G2pNotImplementedError,
                                "VoicebankSession::convertG2p: no LanguageService configured");
    }
    // Delegate to LanguageService::convert(packageId, singerId, language, inputs).
    // The service handles route resolution; per-lyric failures are surfaced
    // via G2pRes::isFailed() rather than Expected (R6: don't lose details).
    return svc->convert(singerKey.packageId, singerKey.singerId, language, inputs);
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
    if (!findSinger(*snap, singerKey)) {
        return srt::core::Error::inferenceError(
            srt::core::ErrorCode::SvsSingerNotFound,
            "VoicebankSession::convertS2p: singer not found: " + singerKey.toString(),
            singerKey.singerId);
    }
    const auto svc = languageService();
    if (!svc) {
        return srt::core::Error(srt::core::ErrorCode::G2pNotImplementedError,
                                "VoicebankSession::convertS2p: no LanguageService configured");
    }
    // Resolve the cached S2P resource, then run the conversion.
    auto resExp = svc->resolveS2pResource(singerKey.packageId, singerKey.singerId, language);
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
                                std::string("VoicebankSession::convertS2p: ") + e.what());
    } catch (...) {
        return srt::core::Error(srt::core::ErrorCode::S2pConversionFailed,
                                "VoicebankSession::convertS2p: unknown S2P conversion failure");
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
    const auto *singer = findSinger(*snap, singerKey);
    if (!singer) {
        return srt::core::Error::inferenceError(
            srt::core::ErrorCode::SvsSingerNotFound,
            "VoicebankSession::validatePhonemes: singer not found: " + singerKey.toString(),
            singerKey.singerId);
    }
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
    const auto *singer = findSinger(*snap, singerKey);
    if (!singer) {
        return srt::core::Error::inferenceError(
            srt::core::ErrorCode::SvsSingerNotFound,
            "VoicebankSession::createModelSet: singer not found: " + singerKey.toString(),
            singerKey.singerId);
    }
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
    auto handle = std::shared_ptr<ModelSetHandle>(
        new ModelSetHandle(std::move(modelSet), gen, std::move(isCurrentGen)));
    return handle;
}

} // namespace ds::session
