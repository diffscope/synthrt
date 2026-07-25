// test_vbs_snapshot.cpp
// WP7 V3 sync contract: snapshot fingerprint behavior for
// ds::session::VoicebankSession.
//
// Covers catalogFingerprint / languageFingerprint stability and change
// detection (V3-07 D-33). L1 tests do not load plugin DLLs.
//
// Split from test_voicebank_session_hardening.cpp for parallel compile/run.

#include <filesystem>
#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Core/Runtime.h>
#include <diffsinger/Session/VoicebankSession.h>

#include "test_vbs_common.h"

using namespace vbs_test;

// ===========================================================================
// B. Snapshot fingerprint contract (WP2 contract)
// ===========================================================================

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
    // Contract (V3-07 D-33): languageFingerprint only covers language-route-
    // relevant fields (packageId, version, rootPath, singer defaultLanguage,
    // singer languages[]). reservedPhonemes is a session-level config input
    // and is NOT part of languageFingerprint.
    //
    // The `changed` flag tracks reservedPhonemes transitions separately via
    // `previous->reservedPhonemes != next->reservedPhonemes` (see
    // VoicebankSession.cpp:543), so a reservedPhonemes change still flips
    // `changed=true` even though both fingerprints are byte-identical.
    //
    // Verify the invariant at runtime: hold package/singer content constant,
    // flip reservedPhonemes, and assert (1) languageFingerprint is unchanged
    // and (2) `changed` is true.
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    session.setReservedPhonemes({"a", "b"});
    const auto first = session.refreshAsync().get();
    REQUIRE(first.succeeded);
    REQUIRE_FALSE(first.snapshot->languageFingerprint.empty());
    const auto fingerprintBefore = first.snapshot->languageFingerprint;

    // Same roots, same package content — only reservedPhonemes differs.
    session.setReservedPhonemes({"c", "d", "e"});
    const auto second = session.refreshAsync().get();
    REQUIRE(second.succeeded);
    REQUIRE(second.snapshot->languageFingerprint == fingerprintBefore);
    // `changed` must be true: reservedPhonemes differs even though the
    // language fingerprint is byte-identical.
    REQUIRE(second.changed);

    std::filesystem::remove_all(root);
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
