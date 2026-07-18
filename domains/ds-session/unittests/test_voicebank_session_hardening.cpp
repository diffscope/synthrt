// test_voicebank_session_hardening.cpp
// WP9: Hardening regression tests for ds::session::VoicebankSession.
//
// Covers the synchronization contracts from docs/refactoring-vnext/
// 05-test-strategy-and-edge-cases.md: refresh coalescing, snapshot stability,
// capabilitySummary/validatePhonemes error paths, createModelSet error paths,
// and concurrent read/write safety. L1 tests do not load plugin DLLs; tests
// that need a real ONNX-backed capabilityReport or a Runtime with loaded
// packages are marked SKIP() and deferred to L2.

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
