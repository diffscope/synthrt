// test_vbs_language.cpp
// WP7 V3 sync contract: ensureLanguageReady / convertG2p / convertS2p error
// paths, multi-version routing ambiguity (V3-10), and updateMetadata
// hot-reload limits (V3-16 §4.2.4/§4.2.5).
//
// L1 tests do not load plugin DLLs or Runtime ONNX packages; tests needing
// those are marked SKIP() with explicit L2 fixture requirements documented
// in comments. Tagged [ds-session][v3].
//
// Split from test_voicebank_session_hardening.cpp for parallel compile/run.

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/G2P/LanguageService.h>
#include <diffsinger/Session/VoicebankSession.h>

#include "test_vbs_common.h"

using namespace vbs_test;

// ===========================================================================
// A. ensure* API error paths (WP4 contract)
// ===========================================================================

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

// ===========================================================================
// C. Multi-version routing ambiguity (WP5 contract)
// ===========================================================================

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

TEST_CASE("convertS2p returns G2pVersionAmbiguous for multi-version packageId without version",
          "[ds-session][v3]") {
    // Contract (V3-10): when a packageId has multiple versions in snapshot
    // and the caller omits version, convertS2p routes through the version-
    // aware LanguageService::resolveS2pResource → resolveLanguageRoute, which
    // returns G2pVersionAmbiguous for multi-version same-packageId routes.
    //
    // Unlike convertG2p, S2P resource resolution only needs metadata (Stage 1,
    // no ONNX models), so L1 can exercise the ambiguity path with a real
    // LanguageService initialized via initializeMetadata.
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

    // Initialize a LanguageService with the same multi-version packages so
    // resolveS2pResource can route through resolveLanguageRoute. Stage 1 only
    // (no ONNX models) — sufficient for route resolution and S2P cache lookup.
    auto langSvc = std::make_shared<srt::g2p::LanguageService>();
    std::vector<srt::g2p::PackageDirectoryEntry> entries = {
        {"session.dup", stdc::VersionNumber::fromString("1.0.0"), root / "bank"},
        {"session.dup", stdc::VersionNumber::fromString("2.0.0"), root / "bank2"},
    };
    REQUIRE(langSvc->initializeMetadata({}, {}, entries).hasValue());
    session.setLanguageService(langSvc);

    // Empty version + multi-version → G2pVersionAmbiguous from
    // resolveLanguageRoute, surfaced through resolveS2pResource.
    ds::bank::SingerRef ref("session.dup", "v1");  // empty version
    auto exp = session.convertS2p(ref, "cmn", "ni");
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::G2pVersionAmbiguous));

    std::filesystem::remove_all(root);
}

TEST_CASE("convertS2p with explicit version routes to that version",
          "[ds-session][v3]") {
    // Contract (V3-10): caller provides version → convertS2p delegates to the
    // version-aware resolveS2pResource, which routes to the exact
    // (packageId, version) entry via resolveLanguageRoute. With explicit
    // version the ambiguity check is bypassed.
    //
    // The makeSamePackageIdTwoVersions fixture has no S2P data (no s2pMode /
    // dict), so resolveS2pResource fails at the S2P resource construction
    // step with S2pResourceNotFound — but crucially NOT with
    // G2pVersionAmbiguous, proving the route was resolved to the requested
    // version. L1 cannot exercise the actual S2P conversion (needs a dict
    // file or ONNX model); see test_resolve_s2p_resource.cpp for convert()
    // coverage.
    const auto root = makeRoot();
    makeSamePackageIdTwoVersions(root);
    ds::session::VoicebankSession session;
    session.setRoots({root / "bank", root / "bank2"});
    REQUIRE(session.refreshAsync().get().succeeded);

    auto langSvc = std::make_shared<srt::g2p::LanguageService>();
    std::vector<srt::g2p::PackageDirectoryEntry> entries = {
        {"session.dup", stdc::VersionNumber::fromString("1.0.0"), root / "bank"},
        {"session.dup", stdc::VersionNumber::fromString("2.0.0"), root / "bank2"},
    };
    REQUIRE(langSvc->initializeMetadata({}, {}, entries).hasValue());
    session.setLanguageService(langSvc);

    // Explicit version → route resolves to the exact (packageId, version)
    // entry, then S2P resource lookup fails (no s2pMode configured in the
    // fixture). The SingerRef.version string must match the snapshot's
    // singer version string (VersionNumber normalizes "1.0.0" to "1.0"), so
    // we read it from the snapshot to stay robust against normalization.
    const auto snap = session.snapshot();
    REQUIRE(snap);
    std::string v1Version, v2Version;
    for (const auto &s : snap->singers) {
        if (s.ref.singerId == "v1") v1Version = s.ref.version;
        if (s.ref.singerId == "v2") v2Version = s.ref.version;
    }
    REQUIRE_FALSE(v1Version.empty());
    REQUIRE_FALSE(v2Version.empty());
    REQUIRE(v1Version != v2Version);

    // v1: explicit version routes past the ambiguity check.
    ds::bank::SingerRef ref("session.dup", "v1", v1Version);
    auto exp = session.convertS2p(ref, "cmn", "ni");
    REQUIRE_FALSE(exp.hasValue());
    // S2pResourceNotFound (not G2pVersionAmbiguous) proves the version-aware
    // route resolved successfully before reaching the S2P resource step.
    REQUIRE(exp.isError(srt::core::ErrorCode::S2pResourceNotFound));

    // v2: explicit version routes to the v2 package.
    ds::bank::SingerRef ref2("session.dup", "v2", v2Version);
    auto exp2 = session.convertS2p(ref2, "cmn", "ni");
    REQUIRE_FALSE(exp2.hasValue());
    REQUIRE(exp2.isError(srt::core::ErrorCode::S2pResourceNotFound));

    std::filesystem::remove_all(root);
}

// ===========================================================================
// E. updateMetadata hot-reload limits (WP8 contract, §4.2.5)
// ===========================================================================

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
