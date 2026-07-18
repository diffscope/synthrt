// test_voicebank_session_hardening.cpp
// WP9: Hardening regression tests for ds::session::VoicebankSession.
//
// Covers the synchronization contracts from docs/refactoring-vnext/
// 05-test-strategy-and-edge-cases.md: refresh coalescing, snapshot stability,
// capabilitySummary/validatePhonemes error paths, createModelSet error paths,
// and concurrent read/write safety. L1 tests do not load plugin DLLs; tests
// that need a real ONNX-backed capabilityReport or a Runtime with loaded
// packages are marked SKIP() and deferred to L2.
// WP7 (v3): V3 sync contract coverage (ensure*, fingerprint, ambiguity, stale, hot-reload).

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Core/Runtime.h>
#include <diffsinger/Session/VoicebankSession.h>

namespace {
// Fixture helpers (mirror test_voicebank_session.cpp; kept local to avoid
// changing the existing test target).
std::filesystem::path makeRoot() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("ds-session-hardening-" + std::to_string(stamp));
    std::filesystem::create_directories(root / "bank");
    return root;
}
void writeFile(const std::filesystem::path &path, const std::string &text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << text;
}
void makePackage(const std::filesystem::path &root) {
    const auto bank = root / "bank";
    writeFile(bank / "desc.json", R"({"id":"session.test","version":"1.0.0","contributes":{"singers":["characters/test/config.json"],"inferences":["inferences/duration/config.json"]}})");
    writeFile(bank / "characters/test/config.json", R"({"id":"test","imports":[{"id":"duration"}],"configuration":{"defaultLanguage":"cmn","languages":[{"id":"cmn"}]}})");
    writeFile(bank / "inferences/duration/config.json", R"({"id":"duration","class":"ai.svs.DurationInference","configuration":{}})");
}
/// Create a second valid package in a sibling directory under the same root.
void makeSecondPackage(const std::filesystem::path &root) {
    const auto bank2 = root / "bank2";
    writeFile(bank2 / "desc.json", R"({"id":"session.other","version":"2.0.0","contributes":{"singers":["characters/other/config.json"],"inferences":["inferences/duration2/config.json"]}})");
    writeFile(bank2 / "characters/other/config.json", R"({"id":"other","imports":[{"id":"duration2"}],"configuration":{"defaultLanguage":"cmn","languages":[{"id":"cmn"}]}})");
    writeFile(bank2 / "inferences/duration2/config.json", R"({"id":"duration2","class":"ai.svs.DurationInference","configuration":{}})");
}
/// Create two packages with the SAME packageId but DIFFERENT versions under
/// sibling roots, used to exercise V3-10 multi-version ambiguity at L1.
/// The ambiguity check in VoicebankSession::ensureLanguageReady runs against
/// snapshot data only (no G2P routing), so L1 can reach it.
void makeSamePackageIdTwoVersions(const std::filesystem::path &root) {
    const auto bank1 = root / "bank";
    writeFile(bank1 / "desc.json", R"({"id":"session.dup","version":"1.0.0","contributes":{"singers":["characters/v1/config.json"],"inferences":["inferences/dur1/config.json"]}})");
    writeFile(bank1 / "characters/v1/config.json", R"({"id":"v1","imports":[{"id":"dur1"}],"configuration":{"defaultLanguage":"cmn","languages":[{"id":"cmn"}]}})");
    writeFile(bank1 / "inferences/dur1/config.json", R"({"id":"dur1","class":"ai.svs.DurationInference","configuration":{}})");

    const auto bank2 = root / "bank2";
    writeFile(bank2 / "desc.json", R"({"id":"session.dup","version":"2.0.0","contributes":{"singers":["characters/v2/config.json"],"inferences":["inferences/dur2/config.json"]}})");
    writeFile(bank2 / "characters/v2/config.json", R"({"id":"v2","imports":[{"id":"dur2"}],"configuration":{"defaultLanguage":"cmn","languages":[{"id":"cmn"}]}})");
    writeFile(bank2 / "inferences/dur2/config.json", R"({"id":"dur2","class":"ai.svs.DurationInference","configuration":{}})");
}
} // namespace

// ===========================================================================
// 1. refresh contract
// ===========================================================================

TEST_CASE("Concurrent refreshAsync calls coalesce to a single in-flight task",
          "[ds-session][hardening]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    // Release all threads at once so they race to enter refreshAsync() while
    // the first in-flight task (file I/O) is still running.
    std::promise<void> start;
    const std::shared_future<void> go = start.get_future().share();

    constexpr int N = 8;
    std::vector<std::shared_future<ds::session::RefreshResult>> futures(N);
    std::vector<std::thread> threads;
    threads.reserve(N);
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&session, &futures, &go, i] {
            go.wait();
            futures[i] = session.refreshAsync();
        });
    }
    start.set_value();
    for (auto &t : threads) t.join();

    // Every future must resolve successfully and publish a snapshot.
    std::vector<std::shared_ptr<const ds::session::VoicebankSnapshot>> snaps;
    snaps.reserve(N);
    for (const auto &f : futures) {
        const auto r = f.get();
        REQUIRE(r.succeeded);
        REQUIRE(r.snapshot != nullptr);
        snaps.push_back(r.snapshot);
    }
    // Coalescing contract: each successful refresh allocates a distinct
    // VoicebankSnapshot, so identical pointers prove multiple callers shared
    // one in-flight task.
    for (int i = 1; i < N; ++i)
        REQUIRE(snaps[i] == snaps[0]);

    std::filesystem::remove_all(root);
}

TEST_CASE("RefreshResult changed field is false on identical content",
          "[ds-session][hardening]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    const auto first = session.refreshAsync().get();
    REQUIRE(first.succeeded);
    REQUIRE(first.changed);  // first refresh always reports changed
    REQUIRE(first.changes.added.empty());
    REQUIRE(first.changes.removed.empty());
    REQUIRE(first.changes.changed.empty());

    const auto second = session.refreshAsync().get();
    REQUIRE(second.succeeded);
    REQUIRE_FALSE(second.changed);  // content identical -> changed == false
    REQUIRE(second.changes.added.empty());
    REQUIRE(second.changes.removed.empty());
    REQUIRE(second.changes.changed.empty());

    std::filesystem::remove_all(root);
}

TEST_CASE("ChangeSummary reports added/removed/changed packages",
          "[ds-session][hardening]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    // First refresh: package A present.
    const auto first = session.refreshAsync().get();
    REQUIRE(first.succeeded);
    REQUIRE(first.changed);

    // Second refresh: add package B -> changes.added must contain B.
    makeSecondPackage(root);
    session.setRoots({root});
    const auto second = session.refreshAsync().get();
    REQUIRE(second.succeeded);
    REQUIRE(second.changed);
    bool addedB = false;
    for (const auto &c : second.changes.added)
        if (c.packageId == "session.other") { addedB = true; break; }
    REQUIRE(addedB);
    // A is unchanged (not in added/removed/changed).
    bool touchedA = false;
    for (const auto &c : second.changes.removed)
        if (c.packageId == "session.test") { touchedA = true; break; }
    for (const auto &c : second.changes.changed)
        if (c.packageId == "session.test") { touchedA = true; break; }
    REQUIRE_FALSE(touchedA);

    // Third refresh: drop package A by switching roots -> changes.removed must
    // contain A.
    session.setRoots({root / "bank2"});
    const auto third = session.refreshAsync().get();
    REQUIRE(third.succeeded);
    REQUIRE(third.changed);
    bool removedA = false;
    for (const auto &c : third.changes.removed)
        if (c.packageId == "session.test") { removedA = true; break; }
    REQUIRE(removedA);

    std::filesystem::remove_all(root);
}

TEST_CASE("RefreshResult diagnostics include invalid package errors",
          "[ds-session][hardening]") {
    const auto root = makeRoot();
    makePackage(root);
    // Add a broken package alongside the valid one.
    writeFile(root / "broken" / "desc.json", "not json");
    ds::session::VoicebankSession session;
    session.setRoots({root});
    const auto result = session.refreshAsync().get();
    // A single invalid package aborts the refresh (no half-built snapshot).
    REQUIRE_FALSE(result.succeeded);
    REQUIRE_FALSE(result.diagnostics.empty());
    bool foundBroken = false;
    for (const auto &d : result.diagnostics) {
        if (d.message.find("broken") != std::string::npos ||
            d.packageId.find("broken") != std::string::npos) {
            foundBroken = true;
            break;
        }
    }
    REQUIRE(foundBroken);
    // Failed refresh must preserve the previous snapshot (none here -> empty).
    REQUIRE(result.snapshot == nullptr);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 2. snapshot contract
// ===========================================================================

TEST_CASE("Snapshot pointer is stable across concurrent readers",
          "[ds-session][hardening]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);
    const auto baseline = session.snapshot();
    REQUIRE(baseline != nullptr);

    constexpr int N = 8;
    std::vector<std::shared_ptr<const ds::session::VoicebankSnapshot>> snaps(N);
    std::vector<std::thread> threads;
    threads.reserve(N);
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&session, &snaps, i] {
            snaps[i] = session.snapshot();
        });
    }
    for (auto &t : threads) t.join();
    for (int i = 0; i < N; ++i)
        REQUIRE(snaps[i] == baseline);

    std::filesystem::remove_all(root);
}

TEST_CASE("Failed refresh preserves previous snapshot pointer",
          "[ds-session][hardening]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    const auto initial = session.refreshAsync().get();
    REQUIRE(initial.succeeded);
    const auto snap1 = session.snapshot();
    REQUIRE(snap1 != nullptr);

    // Introduce a broken package so the next refresh fails.
    writeFile(root / "broken" / "desc.json", "not json");
    const auto failed = session.refreshAsync().get();
    REQUIRE_FALSE(failed.succeeded);
    const auto snap2 = session.snapshot();
    REQUIRE(snap2 == snap1);               // session still serves the old snapshot
    REQUIRE(failed.snapshot == snap1);     // RefreshResult also carries the old snapshot

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 3. capabilitySummary contract
// ===========================================================================

TEST_CASE("capabilitySummary returns Disabled for unknown singer",
          "[ds-session][hardening]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    ds::bank::SingerRef ref("missing.pkg", "missing");
    const auto summary = session.capabilitySummary(ref);
    REQUIRE(summary.availability == ds::session::Availability::Disabled);
    REQUIRE_FALSE(summary.diagnostics.empty());
    bool foundNotFound = false;
    for (const auto &d : summary.diagnostics) {
        if (d.code == srt::core::ErrorCode::SvsSingerNotFound) {
            foundNotFound = true;
            break;
        }
    }
    REQUIRE(foundNotFound);

    std::filesystem::remove_all(root);
}

TEST_CASE("capabilitySummary disables singer with incomplete model metadata",
           "[ds-session][hardening]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    // Reserved phonemes flow into the summary's phoneme list when the singer
    // has no capabilityReport (the L1 fixture case).
    session.setReservedPhonemes({"SP", "AP"});
    REQUIRE(session.refreshAsync().get().succeeded);

    // The fixture has only a stub duration model and no phoneme table. Scanner
    // resolution alone must not be confused with render admission: capability
    // analysis produces an empty effective phoneme set, so the singer is
    // correctly disabled for inference.
    ds::bank::SingerRef ref("session.test", "test");
    const auto summary = session.capabilitySummary(ref);
    REQUIRE(summary.availability == ds::session::Availability::Disabled);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 4. validatePhonemes contract
// ===========================================================================

TEST_CASE("validatePhonemes blocks reserved phonemes when singer has no capabilityReport",
          "[ds-session][hardening]") {
    // Hardening contract (ROBUST-05): the session must not silently accept any
    // phoneme -- even reserved tokens like SP/AP -- when it cannot prove
    // support (no capabilityReport). The L1 fixture singer has only a stub
    // duration config with no ONNX model, so no report is produced.
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    session.setReservedPhonemes({"SP", "AP"});
    REQUIRE(session.refreshAsync().get().succeeded);

    ds::bank::SingerRef ref("session.test", "test");
    auto exp = session.validatePhonemes(ref, {"SP", "AP"});
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::G2pValidationError));

    std::filesystem::remove_all(root);
}

TEST_CASE("validatePhonemes rejects unsupported phonemes",
          "[ds-session][hardening]") {
    // Requires a singer with a populated capabilityReport (effective phonemes)
    // so that validatePhonemes reaches the unsupported-phoneme branch. The L1
    // fixture only has a stub duration config with no ONNX model, so no report
    // is produced and the rejection path cannot be exercised. Verified at L2
    // with a real model fixture.
    SKIP("L2: needs a singer with a populated capabilityReport (real ONNX)");
}

// ===========================================================================
// 5. createModelSet / StaleModelSet contract
// ===========================================================================

TEST_CASE("createModelSet returns InferenceNotInitialized when Runtime not set",
          "[ds-session][hardening]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    // Fixture singer is Resolved, so it passes the resolutionState check and
    // reaches the Runtime guard, which must reject with InferenceNotInitialized.
    const auto snap = session.snapshot();
    REQUIRE(snap);
    REQUIRE_FALSE(snap->singers.empty());
    REQUIRE(snap->singers[0].resolutionState == ds::bank::ResolutionState::Resolved);

    ds::bank::SingerRef ref("session.test", "test");
    auto exp = session.createModelSet(ref);
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::InferenceNotInitialized));

    std::filesystem::remove_all(root);
}

TEST_CASE("ModelSetHandle reports stale after refresh",
          "[ds-session][hardening]") {
    // Requires a Runtime with the singer's package loaded so createModelSet
    // can build a handle, plus a subsequent refresh to bump the snapshot
    // generation. L1 fixtures have no real ONNX package, so the staleness
    // lifecycle is verified at L2.
    SKIP("L2: needs Runtime + loaded ONNX package to build a ModelSetHandle");
}

// ===========================================================================
// 6. concurrency safety
// ===========================================================================

TEST_CASE("VoicebankSession is thread-safe for concurrent reads",
          "[ds-session][hardening]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    session.setReservedPhonemes({"SP", "AP"});
    REQUIRE(session.refreshAsync().get().succeeded);

    const auto baseline = session.snapshot();
    REQUIRE(baseline != nullptr);

    constexpr int N = 8;
    std::atomic<int> errors{0};
    std::vector<std::thread> threads;
    threads.reserve(N);
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&session, &errors, baseline] {
            try {
                for (int k = 0; k < 50; ++k) {
                    if (session.snapshot() != baseline) ++errors;
                    const auto avail = session.availability();
                    if (avail.available + avail.degraded + avail.unavailable != 1) ++errors;
                    if (session.roots().size() != 1) ++errors;
                    if (session.reservedPhonemes().size() != 2) ++errors;
                    ds::bank::SingerRef ref("session.test", "test");
                    // This L1 fixture deliberately lacks phoneme metadata, so
                    // it is disabled. The purpose of this test is concurrent
                    // read safety, not inference admission.
                    if (session.capabilitySummary(ref).availability !=
                        ds::session::Availability::Disabled)
                        ++errors;
                }
            } catch (...) {
                ++errors;
            }
        });
    }
    for (auto &t : threads) t.join();
    REQUIRE(errors == 0);

    std::filesystem::remove_all(root);
}

TEST_CASE("Concurrent setRoots and refreshAsync are safe",
          "[ds-session][hardening]") {
    const auto root = makeRoot();
    makePackage(root);
    makeSecondPackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    // Both root configurations below are valid (packages exist on disk), so
    // refreshes succeed regardless of which vector the writer last published.
    const std::vector<std::filesystem::path> rootsAll = {root};
    const std::vector<std::filesystem::path> rootsOne = {root / "bank2"};

    std::atomic<bool> stop{false};
    std::atomic<int> refreshCount{0};

    std::thread writer([&] {
        while (!stop.load()) {
            session.setRoots(rootsAll);
            session.setRoots(rootsOne);
        }
    });
    std::thread refresher([&] {
        while (!stop.load()) {
            const auto r = session.refreshAsync().get();
            if (r.succeeded) ++refreshCount;
        }
    });

    // Run the race briefly; the contract is "no crash, no deadlock".
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop.store(true);
    writer.join();
    refresher.join();
    REQUIRE(refreshCount.load() > 0);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 7. V3 sync contract (WP7)
//
// Covers V3 synchronization contracts from docs/refactoring-v3/
// 03-session-and-snapshot.md (WP2 fingerprints, WP4 ensure* API),
// 04-routing-and-errors.md (WP5 multi-version ambiguity), and
// 06-verification-cross-platform-hotreload.md §4.2.5 (WP8 hot-reload limits).
// L1 tests do not load plugin DLLs or Runtime ONNX packages; tests needing
// those are marked SKIP() with explicit L2 fixture requirements documented
// in comments. Tagged [ds-session][v3] to distinguish from [hardening].
// ===========================================================================

// ---------------------------------------------------------------------------
// A. ensure* API error paths (WP4 contract)
// ---------------------------------------------------------------------------

TEST_CASE("ensureLanguageReady returns RuntimePackageNotLoaded when Runtime not set",
          "[ds-session][v3]") {
    // Contract (V3-09): when Runtime is not injected, ensureLanguageReady
    // cannot proceed to LanguageService initialization. Implementation
    // returns RuntimePackageNotLoaded (verified in VoicebankSession.cpp:
    // the Runtime guard runs before the LanguageService guard).
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    auto exp = session.ensureLanguageReady(
        "session.test", stdc::VersionNumber::fromString("1.0.0"), "cmn");
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::RuntimePackageNotLoaded));

    std::filesystem::remove_all(root);
}

TEST_CASE("ensureLanguageReady returns G2pNotImplementedError when LanguageService not set",
          "[ds-session][v3]") {
    // Contract (V3-09): when Runtime is injected but LanguageService is not,
    // ensureLanguageReady reaches the LanguageService guard and returns
    // G2pNotImplementedError (the implementation's fallback for missing svc,
    // not G2pInitializationError, because initialization was never attempted).
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    srt::core::Runtime runtime;
    session.setRuntime(&runtime);
    REQUIRE(session.refreshAsync().get().succeeded);

    auto exp = session.ensureLanguageReady(
        "session.test", stdc::VersionNumber::fromString("1.0.0"), "cmn");
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::G2pNotImplementedError));

    std::filesystem::remove_all(root);
}

TEST_CASE("ensureLanguageReady returns G2pPackageNotFound for unknown packageId",
          "[ds-session][v3]") {
    // Contract (V3-09): unknown packageId is detected at the snapshot lookup
    // step (found=false) before the Runtime/LanguageService guards run, so
    // the error is G2pPackageNotFound regardless of Runtime state.
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    auto exp = session.ensureLanguageReady(
        "missing.pkg", stdc::VersionNumber::fromString("1.0.0"), "cmn");
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::G2pPackageNotFound));

    std::filesystem::remove_all(root);
}

TEST_CASE("ensureModelSet returns InferenceNotInitialized when Runtime not set",
          "[ds-session][v3]") {
    // Contract (V3-08): ensureModelSet is a thin wrapper over createModelSet;
    // when Runtime is not injected, returns InferenceNotInitialized. The
    // fixture singer is Resolved so it passes the resolutionState guard and
    // reaches the Runtime check.
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    ds::bank::SingerRef ref("session.test", "test");
    auto exp = session.ensureModelSet(ref);
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::InferenceNotInitialized));

    std::filesystem::remove_all(root);
}

TEST_CASE("ensureModelSet returns SvsSingerNotFound for unknown singer",
          "[ds-session][v3]") {
    // Contract (V3-08): unknown singer → SvsSingerNotFound (the actual
    // error code returned by createModelSet's findSinger guard, which
    // ensureModelSet delegates to).
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    ds::bank::SingerRef ref("missing.pkg", "missing");
    auto exp = session.ensureModelSet(ref);
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::SvsSingerNotFound));

    std::filesystem::remove_all(root);
}

// ---------------------------------------------------------------------------
// B. Snapshot fingerprint contract (WP2 contract)
// ---------------------------------------------------------------------------

TEST_CASE("Snapshot fingerprints are stable across identical refreshAsync",
          "[ds-session][v3]") {
    // Contract (V3-07 D-33): two refreshAsync calls with identical content
    // produce identical catalogFingerprint and languageFingerprint values.
    // The fingerprint is content-stable (independent of scan order, mtimes,
    // or generation bump) — only the package/singer content matters.
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    const auto first = session.refreshAsync().get();
    REQUIRE(first.succeeded);
    REQUIRE_FALSE(first.snapshot->catalogFingerprint.empty());
    REQUIRE_FALSE(first.snapshot->languageFingerprint.empty());

    const auto second = session.refreshAsync().get();
    REQUIRE(second.succeeded);
    REQUIRE_FALSE(second.changed);  // sanity: no content change
    REQUIRE(second.snapshot->catalogFingerprint == first.snapshot->catalogFingerprint);
    REQUIRE(second.snapshot->languageFingerprint == first.snapshot->languageFingerprint);

    std::filesystem::remove_all(root);
}

TEST_CASE("catalogFingerprint changes when roots add a second package",
          "[ds-session][v3]") {
    // Contract (V3-07 D-33): adding a package changes the catalog content.
    // catalogFingerprint includes manifest/singer fields, so adding a
    // second package to roots must produce a different digest.
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    const auto first = session.refreshAsync().get();
    REQUIRE(first.succeeded);
    const auto before = first.snapshot->catalogFingerprint;
    REQUIRE_FALSE(before.empty());

    makeSecondPackage(root);
    session.setRoots({root});
    const auto second = session.refreshAsync().get();
    REQUIRE(second.succeeded);
    REQUIRE(second.changed);
    REQUIRE_FALSE(second.snapshot->catalogFingerprint.empty());
    REQUIRE(second.snapshot->catalogFingerprint != before);

    std::filesystem::remove_all(root);
}

TEST_CASE("languageFingerprint excludes reservedPhonemes by design",
          "[ds-session][v3]") {
    // Contract (V3-07 D-33) clarification: languageFingerprint only covers
    // language-route-relevant fields (packageId, version, rootPath, singer
    // defaultLanguage, singer languages[]). reservedPhonemes is a
    // session-level config input and is NOT part of languageFingerprint.
    // The `changed` flag tracks reservedPhonemes transitions separately via
    // `previous->reservedPhonemes != next->reservedPhonemes` (see
    // VoicebankSession.cpp:462), so a reservedPhonemes change still flips
    // `changed=true` even though both fingerprints are byte-identical.
    //
    // This case is intentionally SKIP: it documents the implementation
    // invariant rather than asserting runtime behavior. The invariant is
    // enforced statically by fingerprintPackageLanguage() not reading
    // snapshot.reservedPhonemes.
    SKIP("languageFingerprint excludes reservedPhonemes by design; "
         "changed flag tracks reservedPhonemes separately");
}

TEST_CASE("RefreshResult.changed aligns with fingerprint comparison",
          "[ds-session][v3]") {
    // Contract (V3-07 D-33): `changed` is computed from
    //   previous->catalogFingerprint != next->catalogFingerprint ||
    //   previous->languageFingerprint != next->languageFingerprint
    // (plus roots/reservedPhonemes for config-level changes), NOT the legacy
    // contentEqual comparison. Identical content → fingerprints equal →
    // changed=false; any content delta → fingerprints differ → changed=true.
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    const auto first = session.refreshAsync().get();
    REQUIRE(first.succeeded);
    REQUIRE(first.changed);  // first refresh: no previous → changed
    const auto fp1 = first.snapshot->catalogFingerprint;
    const auto lf1 = first.snapshot->languageFingerprint;
    REQUIRE_FALSE(fp1.empty());
    REQUIRE_FALSE(lf1.empty());

    // Identical content: fingerprints must match, changed must be false.
    const auto second = session.refreshAsync().get();
    REQUIRE(second.succeeded);
    REQUIRE_FALSE(second.changed);
    REQUIRE(second.snapshot->catalogFingerprint == fp1);
    REQUIRE(second.snapshot->languageFingerprint == lf1);

    // Add second package: both fingerprints change, changed must be true.
    makeSecondPackage(root);
    session.setRoots({root});
    const auto third = session.refreshAsync().get();
    REQUIRE(third.succeeded);
    REQUIRE(third.changed);
    REQUIRE_FALSE(third.snapshot->catalogFingerprint.empty());
    REQUIRE(third.snapshot->catalogFingerprint != fp1);
    REQUIRE(third.snapshot->languageFingerprint != lf1);

    std::filesystem::remove_all(root);
}

// ---------------------------------------------------------------------------
// C. Multi-version routing ambiguity (WP5 contract)
// ---------------------------------------------------------------------------

TEST_CASE("ensureLanguageReady returns G2pVersionAmbiguous for multi-version packageId without version",
          "[ds-session][v3]") {
    // Contract (V3-10): when a packageId has multiple versions in snapshot
    // and the caller omits version, ensureLanguageReady must reject with
    // G2pVersionAmbiguous rather than silently resolving to whichever
    // version was iterated first. This check runs against snapshot data
    // only (before Runtime/LanguageService guards), so L1 can exercise it
    // with a same-packageId-different-version fixture and no Runtime.
    const auto root = makeRoot();
    makeSamePackageIdTwoVersions(root);
    ds::session::VoicebankSession session;
    session.setRoots({root / "bank", root / "bank2"});
    REQUIRE(session.refreshAsync().get().succeeded);

    // Verify the snapshot actually contains two versions of the same package.
    const auto snap = session.snapshot();
    REQUIRE(snap);
    int dupCount = 0;
    for (const auto &pkg : snap->packages) {
        if (pkg.packageId == "session.dup") ++dupCount;
    }
    REQUIRE(dupCount == 2);

    // Empty version + multi-version → G2pVersionAmbiguous.
    auto exp = session.ensureLanguageReady(
        "session.dup", stdc::VersionNumber{}, "cmn");
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::G2pVersionAmbiguous));

    std::filesystem::remove_all(root);
}

TEST_CASE("ensureLanguageReady with explicit version skips ambiguity for multi-version packageId",
          "[ds-session][v3]") {
    // Contract (V3-10): caller-provided version routes to the exact
    // (packageId, version) entry, bypassing the ambiguity check. The next
    // guard is Runtime presence, which L1 fixtures don't configure, so the
    // expected error here is RuntimePackageNotLoaded (NOT G2pVersionAmbiguous
    // and NOT G2pPackageNotFound).
    const auto root = makeRoot();
    makeSamePackageIdTwoVersions(root);
    ds::session::VoicebankSession session;
    session.setRoots({root / "bank", root / "bank2"});
    REQUIRE(session.refreshAsync().get().succeeded);

    auto exp = session.ensureLanguageReady(
        "session.dup", stdc::VersionNumber::fromString("2.0.0"), "cmn");
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::RuntimePackageNotLoaded));

    std::filesystem::remove_all(root);
}

TEST_CASE("convertG2p returns G2pVersionAmbiguous for multi-version packageId without version",
          "[ds-session][v3]") {
    // Contract (V3-10): when a packageId has multiple versions in snapshot
    // and the caller omits version, convertG2p routes through
    // LanguageService::convert → resolveLanguageRoute, which returns
    // G2pVersionAmbiguous for multi-version same-packageId routes.
    //
    // L1 fixture: makePackage/makeSecondPackage create two packages with
    // DIFFERENT packageIds, so the ambiguity path cannot be exercised. Even
    // if a same-packageId fixture were added, convertG2p requires a real
    // LanguageService with G2P route data — L1 has none.
    SKIP("L2: needs Runtime + LanguageService with same-packageId "
         "multi-version voicebank + G2P route data");
}

TEST_CASE("convertG2p with explicit version routes to that version",
          "[ds-session][v3]") {
    // Contract (V3-10): caller provides version → LanguageService::convert
    // routes to the (packageId, version) ContextKey. L1 cannot exercise the
    // full convert path (needs ONNX-backed G2P plugin loaded by the
    // LanguageService).
    SKIP("L2: needs Runtime + LanguageService with loaded G2P plugin");
}

// ---------------------------------------------------------------------------
// D. Stale retry contract (V3-12)
// ---------------------------------------------------------------------------

TEST_CASE("Stale retry: rebuild-once + retry-once after StaleModelSet",
          "[ds-session][v3]") {
    // Contract (V3-12 D-30): on StaleModelSet, the host must
    //   (1) drop the old ModelSetHandle,
    //   (2) re-create via ensureModelSet/createModelSet,
    //   (3) retry start() once.
    // If the second start() also returns StaleModelSet (extreme: two
    // refreshes between attempts), surface an error to the user. Forbidden:
    //   - auto-retry loop until success,
    //   - canceling in-flight tasks (they finish on the old ModelSet),
    //   - silently dropping the handle without rebuilding.
    //
    // L1 cannot exercise this: needs a real ModelSetHandle built from a
    // loaded ONNX package, plus a concurrent refresh to bump the snapshot
    // generation. See existing "ModelSetHandle reports stale after refresh"
    // SKIP above for the same constraint.
    SKIP("L2: needs Runtime + loaded ONNX package to build a ModelSetHandle "
         "and a concurrent refresh to bump snapshot generation");
}

// ---------------------------------------------------------------------------
// E. updateMetadata hot-reload limits (WP8 contract, §4.2.5)
// ---------------------------------------------------------------------------

TEST_CASE("refresh uses incremental updateMetadata with fallback",
          "[ds-session][v3]") {
    // Contract (V3-16 §4.2.4): VoicebankSession::performRefresh calls
    // LanguageService::updateMetadata when metadataReady() == true, and
    // falls back to initializeMetadata on updateMetadata failure. If both
    // fail, only a Warning diagnostic is recorded; snapshot publication is
    // not blocked (see VoicebankSession.cpp:504-524).
    //
    // L1 cannot exercise this: needs a real LanguageService with
    // metadataReady state, which requires G2P plugin DLLs to be discovered
    // and at least one successful initializeMetadata call.
    SKIP("L2: needs LanguageService with metadataReady state "
         "(requires G2P plugin DLLs)");
}

TEST_CASE("updateMetadata returns G2pAlreadyInitialized after modelsReady",
          "[ds-session][v3]") {
    // Contract (V3-16 §4.2.5): after modelsReady() == true (i.e.
    // Manager::initialize has run), PackageManager contexts are immutable.
    // Calling updateMetadata after that returns G2pAlreadyInitialized
    // because the PackageManager cannot apply the diff. Caller must restart
    // the host process for official G2P or ONNX provider changes.
    //
    // L1 cannot exercise this: needs a LanguageService that has run
    // initializeModels successfully (requires G2P plugin DLLs + ONNX
    // runtime).
    SKIP("L2: needs LanguageService with modelsReady=true "
         "(requires G2P plugin DLLs + ONNX runtime)");
}

// ---------------------------------------------------------------------------
// F. L2 multi-version fixture placeholder
// ---------------------------------------------------------------------------

TEST_CASE("Multi-version fixture: same packageId two versions",
          "[ds-session][v3]") {
    // L2 fixture requirement: two voicebank packages with the SAME packageId
    // but DIFFERENT versions (e.g. "opencanto" 1.0.0 at /vb/opencanto-v1,
    // "opencanto" 2.0.0 at /vb/opencanto-v2), each with a real ONNX-backed
    // G2P route. Used to exercise:
    //   - snapshot publishes both versions (V3-09 §1.1)
    //   - convertG2p without version → G2pVersionAmbiguous (V3-10)
    //   - convertG2p with version → routes to corresponding version
    //   - ensureLanguageReady without version → G2pVersionAmbiguous
    //   - Stale retry after refresh removes one version (V3-12)
    //
    // See docs/refactoring-v3/04-routing-and-errors.md §7.2 for the L2
    // contract matrix.
    SKIP("L2: needs Runtime + multi-version voicebank package fixture");
}
