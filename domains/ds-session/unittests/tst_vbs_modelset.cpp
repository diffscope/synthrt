// test_vbs_modelset.cpp
// WP9 hardening + WP7 V3: createModelSet / ensureModelSet / ModelSetHandle /
// StaleModelSet lifecycle, plus computeChanges disabled-entry deduplication.
//
// Covers createModelSet/ensureModelSet error paths (InferenceNotInitialized,
// SvsSingerNotFound) and the computeChanges dedup contract for singers
// sharing a coordinate. L1 tests do not load plugin DLLs; the Stale-retry /
// ModelSetHandle success paths (V3-12 D-30) require an L2 fixture and are
// covered elsewhere. No SKIP() shells are retained.
//
// Split from test_voicebank_session_hardening.cpp for parallel compile/run.

#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Core/Runtime.h>
#include <diffsinger/Session/VoicebankSession.h>

#include "test_vbs_common.h"

using namespace vbs_test;

// ===========================================================================
// computeChanges deduplication contract
// ===========================================================================

TEST_CASE("computeChanges does not duplicate disabled entries for singers sharing coordinate",
          "[ds-session][hardening]") {
    // Two singers in the same package share the same PackageCoordinate
    // (packageId + version). When the package is removed, each singer pushes
    // its coordinate to changes.disabled. The deduplication in computeChanges
    // must collapse these to a single entry so hosts do not see the same
    // package reported twice.
    const auto root = makeRoot();

    // Build a package with TWO singers and one inference. The singers have no
    // imports, so SingerCapabilityAnalyzer::analyze returns nullopt (pure G2P
    // path) and availabilityOf returns Available. The package still has an
    // inference so inferenceIds is non-empty, keeping resolutionState Resolved.
    const auto bank = root / "bank";
    writeFile(bank / "desc.json",
              R"({"id":"session.dup","version":"1.0.0","contributes":{"singers":["characters/s1/config.json","characters/s2/config.json"],"inferences":["inferences/dur/config.json"]}})");
    writeFile(bank / "characters/s1/config.json",
              R"({"id":"s1","configuration":{"defaultLanguage":"cmn","languages":[{"id":"cmn"}]}})");
    writeFile(bank / "characters/s2/config.json",
              R"({"id":"s2","configuration":{"defaultLanguage":"cmn","languages":[{"id":"cmn"}]}})");
    writeFile(bank / "inferences/dur/config.json",
              R"({"id":"dur","class":"ai.svs.DurationInference","configuration":{}})");

    ds::session::VoicebankSession session;
    session.setRoots({root});
    const auto first = session.refreshAsync().get();
    REQUIRE(first.succeeded);
    REQUIRE(first.snapshot != nullptr);
    REQUIRE(first.snapshot->singers.size() == 2);
    REQUIRE(first.snapshot->singers[0].ref.packageId == "session.dup");
    REQUIRE(first.snapshot->singers[1].ref.packageId == "session.dup");

    // Second refresh: drop the package by switching roots to an empty dir.
    // Both singers disappear from the next snapshot; each was Available in
    // prev, so each would push coordinateOf(s) to changes.disabled. Without
    // deduplication this yields two identical PackageCoordinate entries.
    const auto empty = root / "empty";
    std::filesystem::create_directories(empty);
    session.setRoots({empty});
    const auto second = session.refreshAsync().get();
    REQUIRE(second.succeeded);
    REQUIRE(second.changed);

    int disabledCount = 0;
    for (const auto &c : second.changes.disabled) {
        if (c.packageId == "session.dup") {
            ++disabledCount;
        }
    }
    REQUIRE(disabledCount == 1);

    std::filesystem::remove_all(root);
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

// ===========================================================================
// A. ensure* API error paths (V3-08 contract)
// ===========================================================================

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

