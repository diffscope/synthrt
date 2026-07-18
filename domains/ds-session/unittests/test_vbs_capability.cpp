// test_vbs_capability.cpp
// WP9 hardening: capabilitySummary + validatePhonemes contracts for
// ds::session::VoicebankSession.
//
// Covers the error paths for capabilitySummary (Disabled for unknown singer,
// Disabled for incomplete model metadata) and validatePhonemes (blocks
// reserved phonemes without capabilityReport, rejects unsupported phonemes).
// L1 tests do not load plugin DLLs.
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
