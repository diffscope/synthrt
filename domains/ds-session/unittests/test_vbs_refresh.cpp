// test_vbs_refresh.cpp
// WP9 hardening: refresh contract + concurrency safety for
// ds::session::VoicebankSession.
//
// Covers refresh coalescing, ChangeSummary reporting, diagnostics for invalid
// packages, snapshot-pointer stability across concurrent readers, and the
// read/write race safety contract. L1 tests do not load plugin DLLs.
//
// Split from test_voicebank_session_hardening.cpp for parallel compile/run.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Core/Runtime.h>
#include <diffsinger/Session/VoicebankSession.h>

#include "test_vbs_common.h"

using namespace vbs_test;

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
    // D-31: a valid package is still published even when an invalid one is
    // present; the refresh succeeds and the invalid package surfaces as a
    // diagnostic.
    REQUIRE(result.succeeded);
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
    // The snapshot must contain the valid package, not the broken one.
    REQUIRE(result.snapshot != nullptr);
    REQUIRE(result.snapshot->packages.size() == 1);
    REQUIRE(result.snapshot->packages[0].packageId == "session.test");
    REQUIRE(result.snapshot->packages[0].valid);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 2. snapshot pointer stability
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

TEST_CASE("Refresh with added invalid package preserves previous snapshot pointer",
          "[ds-session][hardening]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    const auto initial = session.refreshAsync().get();
    REQUIRE(initial.succeeded);
    const auto snap1 = session.snapshot();
    REQUIRE(snap1 != nullptr);

    // D-31: adding a broken package alongside the valid one does not abort
    // the refresh. The valid package is unchanged, so the snapshot content
    // is identical and the published pointer is preserved (no-op refresh).
    writeFile(root / "broken" / "desc.json", "not json");
    const auto refreshed = session.refreshAsync().get();
    REQUIRE(refreshed.succeeded);
    REQUIRE_FALSE(refreshed.diagnostics.empty());
    const auto snap2 = session.snapshot();
    REQUIRE(snap2 == snap1);                 // session still serves the old snapshot
    REQUIRE(refreshed.snapshot == snap1);    // RefreshResult also carries the old snapshot

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 3. concurrency safety
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
