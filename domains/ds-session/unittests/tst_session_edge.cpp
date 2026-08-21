// ds-session edge condition tests (SES-001 ~ SES-012).
//
// Coverage matrix: docs/refactoring-vnext/test-matrix-expansion.md §1.10
// Test target: domains/ds-session/unittests (CMakeLists uses synthrt_add_unittest
//           to explicitly collect, needs tst_session_edge.cpp entry appended).
// Reused fixture: test_vbs_common.h (vbs_test::makeRoot / makePackage /
//                 makeSecondPackage / writeFile).
//
// Design principle cross-reference:
//   ROBUST-01: convertG2p/convertS2p/validatePhonemes/createModelSet/
//              ensureLanguageReady return Expected<T>; refresh returns RefreshResult
//              (failure info filled into .errorMessage / .diagnostics), no exceptions thrown.
//   ROBUST-02: Callback exceptions are isolated at the notifyRefresh boundary (SES-007).
//   INFRA-03:  L1 does not load plugin DLLs; cases requiring real Runtime/ONNX SKIP.
//   ARCH-03:   Host composes VoicebankSession; SessionResources borrows Runtime/LangSvc.
//
// API difference notes (where actual API differs from matrix expectations, adjusted per actual API):
//   SES-001: Matrix expects refresh failure, errorMessage non-empty; actual empty roots (including default
//             session without setRoots) is a valid empty scan, refresh succeeds and publishes
//             empty snapshot (errorMessage empty). The genuine failure path (all packages
//             invalid) is covered by tst_voicebank_session_snapshot_ensure.cpp.
//   SES-002: Matrix expects default session calling convertG2p to return G2pNotImplementedError;
//             actual default session without snapshot first returns SessionError
//             ("no snapshot available"). Need to refresh first so singer exists, to
//             reach LanguageService check and return G2pNotImplementedError. This case
//             tests the latter (matching matrix expected error code), and comments on the former.
//   SES-003: Matrix expects NotFound; actual convertG2p for unknown singer returns
//             ErrorCode::SvsSingerNotFound (SVS segment, not general NotFound).
//   SES-005: VoicebankScanner sets ResolutionState::Resolved for all successfully-parsed
//             singers; L1 fixture cannot construct non-Resolved
//             singer. createModelSet's resolutionState guard is a defensive check,
//             unreachable through normal scanning path -> SKIP.
//   SES-008: Matrix expects OK or InvalidArg; actual L1 fixture singer has a
//             capabilityReport with empty effectivePhonemes (stub duration
//             inference, no phoneme table), validatePhonemes returns
//             G2pValidationError ("has empty effective phonemes").
//   SES-009: Matrix expects repeated calls to return OK; L1 has no Runtime, ensureLanguageReady
//             returns RuntimePackageNotLoaded. This case verifies idempotency (repeated calls return
//             same error, snapshot unchanged); OK success path requires L2.
//
// Error code system see include/synthrt/Core/Support/Diagnostic.h:
//   General: SessionError=9...
//   Inference: InferenceNotInitialized=200, RuntimePackageNotLoaded=218...
//   G2P: G2pNotImplementedError=305, G2pValidationError=307, G2pPackageNotFound=316...
//   SVS: SvsSingerNotFound=600...
//   S2P: S2pResourceNotFound=500...

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/G2P/LanguageService.h>
#include <diffsinger/Bank/SingerRef.h>
#include <diffsinger/Session/ModelSetHandle.h>
#include <diffsinger/Session/VoicebankSession.h>

#include "test_vbs_common.h"

using namespace vbs_test;
using srt::core::ErrorCode;
using srt::core::ErrorCategory;

namespace srt::core {
// Catch2 needs operator<< to stream ErrorCode values in failed assertions.
inline std::ostream &operator<<(std::ostream &os, const ErrorCode &code) {
    return os << errorCodeToString(code);
}
} // namespace srt::core

// ===========================================================================
// SES-001: refresh called without setRoots
//
// Actual behavior: empty roots is a valid empty scan, refresh succeeds and publishes empty snapshot (see file header difference notes)
// ===========================================================================
TEST_CASE("SES-001: refresh on default session without setRoots succeeds with empty snapshot",
          "[session][edge]") {
    ds::session::VoicebankSession session;
    // Default construction: setRoots not called, roots() empty, snapshot() nullptr
    REQUIRE(session.roots().empty());
    REQUIRE(session.snapshot() == nullptr);

    auto result = session.refresh();
    // Matrix expects failure + errorMessage non-empty; actual empty roots succeeds and publishes empty snapshot
    REQUIRE(result.succeeded);
    REQUIRE(result.errorMessage.empty());
    REQUIRE(result.snapshot != nullptr);
    REQUIRE(result.snapshot->packages.empty());
    REQUIRE(result.snapshot->singers.empty());
    REQUIRE(session.snapshot() != nullptr);
}

// ===========================================================================
// SES-002: convertG2p called without injected LanguageService
//
// Need to refresh first so singer exists, to reach LanguageService check -> G2pNotImplementedError
// ===========================================================================
TEST_CASE("SES-002: convertG2p without LanguageService returns G2pNotImplementedError",
          "[session][edge]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session; // Default construction: no LanguageService
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);
    REQUIRE(session.languageService() == nullptr);

    ds::bank::SingerRef ref("session.test", "test");
    std::vector<srt::g2p::G2pInput> inputs{srt::g2p::G2pInput{"ni", "g2p-cmn-official"}};
    auto exp = session.convertG2p(ref, "cmn", inputs);
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::G2pNotImplementedError));
    REQUIRE(exp.error().message().find("LanguageService") != std::string::npos);

    // Note: a fully default session (without refresh) calling convertG2p first returns SessionError
    // ("no snapshot available"), because the snapshot check precedes the LanguageService check.
    std::filesystem::remove_all(root);
}

// ===========================================================================
// SES-003: convertG2p with unknown singerKey
//
// Actual behavior: findSinger not found -> SvsSingerNotFound (see file header difference notes)
// ===========================================================================
TEST_CASE("SES-003: convertG2p with unknown singerKey returns SvsSingerNotFound",
          "[session][edge]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    ds::bank::SingerRef unknownRef("missing.pkg", "missing_singer");
    std::vector<srt::g2p::G2pInput> inputs{srt::g2p::G2pInput{"ni", "g2p-cmn-official"}};
    auto exp = session.convertG2p(unknownRef, "cmn", inputs);
    REQUIRE_FALSE(exp.hasValue());
    // Matrix expects NotFound; actual returns SvsSingerNotFound (SVS segment)
    REQUIRE(exp.isError(ErrorCode::SvsSingerNotFound));
    REQUIRE(exp.errorCategory() == ErrorCategory::SVS);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// SES-004: createModelSet called without injected Runtime
//
// fixture singer is Resolved (VoicebankScanner sets all successfully-parsed singers to Resolved),
// passes resolutionState check and reaches Runtime guard -> InferenceNotInitialized.
// ===========================================================================
TEST_CASE("SES-004: createModelSet without Runtime returns InferenceNotInitialized",
          "[session][edge]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session; // Default construction: no Runtime
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);
    REQUIRE(session.runtime() == nullptr);

    // Confirm fixture singer is Resolved (prerequisite for reaching Runtime guard)
    const auto snap = session.snapshot();
    REQUIRE(snap);
    REQUIRE_FALSE(snap->singers.empty());
    REQUIRE(snap->singers[0].resolutionState == ds::bank::ResolutionState::Resolved);

    ds::bank::SingerRef ref("session.test", "test");
    auto exp = session.createModelSet(ref);
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InferenceNotInitialized));
    REQUIRE(exp.errorCategory() == ErrorCategory::Inference);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// SES-006: refreshAsync concurrent calls
//
// Contract: concurrent calls share the same in-flight scan, returning the same future (snapshot pointers are identical).
// ===========================================================================
TEST_CASE("SES-006: concurrent refreshAsync calls share one scan",
          "[session][edge]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    std::promise<void> start;
    const std::shared_future<void> go = start.get_future().share();

    std::shared_future<ds::session::RefreshResult> f1, f2;
    std::thread t1([&] { go.wait(); f1 = session.refreshAsync(); });
    std::thread t2([&] { go.wait(); f2 = session.refreshAsync(); });
    start.set_value();
    t1.join();
    t2.join();

    const auto r1 = f1.get();
    const auto r2 = f2.get();
    REQUIRE(r1.succeeded);
    REQUIRE(r2.succeeded);
    // Share the same scan: both futures resolve to the same snapshot pointer
    REQUIRE(r1.snapshot != nullptr);
    REQUIRE(r1.snapshot == r2.snapshot);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// SES-007: subscribeRefresh self-reset in callback
//
// Contract: calling subscription.reset() inside the callback is safe, no crash, no further notifications.
// ===========================================================================
TEST_CASE("SES-007: subscribeRefresh self-reset in callback is safe",
          "[session][edge]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    std::atomic<int> calls{0};
    std::atomic<bool> crashed{false};
    ds::session::RefreshSubscription sub;
    sub = session.subscribeRefresh([&](const ds::session::RefreshResult &) {
        ++calls;
        try {
            sub.reset(); // reset self inside callback
        } catch (...) {
            crashed.store(true);
        }
    });

    // First refresh (changed=true): callback triggered, internally resets self
    REQUIRE(session.refreshAsync().get().succeeded);
    REQUIRE_FALSE(crashed.load());
    REQUIRE(calls.load() == 1);
    REQUIRE_FALSE(sub); // inactive after reset

    // Refresh again (content changed): reset subscriber is no longer called back
    makeSecondPackage(root);
    REQUIRE(session.refreshAsync().get().succeeded);
    REQUIRE(calls.load() == 1);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// SES-008: validatePhonemes with empty phonemes
//
// Actual behavior: L1 fixture singer has a capabilityReport with empty
// effectivePhonemes (stub duration inference, no phoneme table) ->
// G2pValidationError ("has empty effective phonemes").
// ===========================================================================
TEST_CASE("SES-008: validatePhonemes with empty phonemes", "[session][edge]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    ds::bank::SingerRef ref("session.test", "test");
    auto exp = session.validatePhonemes(ref, {});
    // Matrix expects OK or InvalidArg; actual L1 fixture has a capabilityReport
    // with empty effectivePhonemes -> G2pValidationError
    // ("has empty effective phonemes; inference blocked")
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::G2pValidationError));
    REQUIRE(exp.error().message().find("effective phonemes") != std::string::npos);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// SES-009: ensureLanguageReady repeated calls (idempotent)
//
// L1 has no Runtime -> RuntimePackageNotLoaded. Verify idempotency: repeated calls return same error,
// snapshot unchanged. OK success path requires L2 (Runtime + LanguageService + model).
// ===========================================================================
TEST_CASE("SES-009: ensureLanguageReady repeated calls are idempotent",
          "[session][edge]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    const auto snap1 = session.snapshot();
    REQUIRE(snap1 != nullptr);

    const auto version = stdc::VersionNumber::fromString("1.0.0").value();
    // First call: no Runtime -> RuntimePackageNotLoaded
    auto exp1 = session.ensureLanguageReady("session.test", version, "cmn");
    REQUIRE_FALSE(exp1.hasValue());
    REQUIRE(exp1.isError(ErrorCode::RuntimePackageNotLoaded));

    // Second call: state unchanged -> same error (idempotent)
    auto exp2 = session.ensureLanguageReady("session.test", version, "cmn");
    REQUIRE_FALSE(exp2.hasValue());
    REQUIRE(exp2.isError(ErrorCode::RuntimePackageNotLoaded));

    // snapshot pointer unchanged (no state change)
    REQUIRE(session.snapshot() == snap1);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// SES-010: snapshot generation increments after refresh
//
// After content changes, refresh again, generation increments.
// ===========================================================================
TEST_CASE("SES-010: snapshot generation increments after content-changing refresh",
          "[session][edge]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    REQUIRE(session.refreshAsync().get().succeeded);
    const auto snap1 = session.snapshot();
    REQUIRE(snap1 != nullptr);
    const auto gen1 = snap1->generation;

    // Add a second package to change content
    makeSecondPackage(root);
    session.setRoots({root});
    const auto second = session.refreshAsync().get();
    REQUIRE(second.succeeded);
    REQUIRE(second.changed); // content changed

    const auto snap2 = session.snapshot();
    REQUIRE(snap2 != nullptr);
    REQUIRE(snap2 != snap1); // new snapshot object
    REQUIRE(snap2->generation > gen1);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// SES-011: convertS2p with pronunciation containing special characters
//
// Inject LanguageService (SessionResources construction + refresh auto-initializes initializeMetadata),
// convertS2p routes "a\n\tb" to S2P resource (fixture has no s2pMode -> S2pResourceNotFound),
// no crash.
// ===========================================================================
TEST_CASE("SES-011: convertS2p with special characters in pronunciation does not crash",
          "[session][edge]") {
    const auto root = makeRoot();
    makePackage(root);

    srt::core::Runtime runtime;
    auto langSvc = std::make_shared<srt::g2p::LanguageService>();
    ds::session::SessionResources resources;
    resources.runtime = &runtime;
    resources.languageService = langSvc;

    ds::session::VoicebankSession session(std::move(resources));
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);
    // refresh automatically calls langSvc->initializeMetadata, metadataReady should be true
    REQUIRE(langSvc->metadataReady());

    ds::bank::SingerRef ref("session.test", "test"); // empty version matches single version
    // pronunciation with newline/tab characters: no crash, returns error (fixture has no S2P data)
    auto exp = session.convertS2p(ref, "cmn", "a\n\tb");
    REQUIRE_FALSE(exp.hasValue());
    // fixture has no s2pMode -> S2pResourceNotFound (routing successfully resolved to package, but no S2P resource)
    REQUIRE(exp.isError(ErrorCode::S2pResourceNotFound));

    std::filesystem::remove_all(root);
}

// ===========================================================================
// SES-012: SessionResources with nullptr runtime
//
// Contract (V3-06): construction stores nullptr resources, session can only be used for discovery;
// createModelSet/convertG2p return corresponding errors. Construction itself does not throw.
// ===========================================================================
TEST_CASE("SES-012: VoicebankSession with null SessionResources is discovery-only",
          "[session][edge]") {
    ds::session::SessionResources resources; // runtime=nullptr, languageService=nullptr
    ds::session::VoicebankSession session(std::move(resources));

    // Construction stores nullptr, does not throw
    REQUIRE(session.runtime() == nullptr);
    REQUIRE(session.languageService() == nullptr);
    REQUIRE(session.snapshot() == nullptr);

    // discovery still usable: refresh publishes empty snapshot
    auto result = session.refresh();
    REQUIRE(result.succeeded);
    REQUIRE(result.snapshot != nullptr);
    REQUIRE(result.snapshot->packages.empty());

    // createModelSet returns InferenceNotInitialized when no Runtime (when no snapshot,
    // first returns SessionError; here after refresh there is an empty snapshot, singer lookup fails returning
    // SvsSingerNotFound). Verify no crash + returns error.
    ds::bank::SingerRef ref("any.pkg", "any_singer");
    auto exp = session.createModelSet(ref);
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::SvsSingerNotFound));
}

// ===========================================================================
// SES-013: default-constructed session's runtime/languageService state
//
// Contract: default-constructed session (without SessionResources) runtime()/languageService()
// are both nullptr, and refresh does not inject resources (refresh only handles discovery, does not modify
// the injected Runtime/LanguageService). Distinct from SES-012 (explicitly passing nullptr via SessionResources
// construction).
// ===========================================================================
TEST_CASE("SES-013: default-constructed session exposes null runtime/languageService that refresh does not inject",
          "[session][edge]") {
    ds::session::VoicebankSession session; // Default construction: no SessionResources
    REQUIRE(session.runtime() == nullptr);
    REQUIRE(session.languageService() == nullptr);
    REQUIRE(session.roots().empty());
    REQUIRE(session.snapshot() == nullptr);

    // refresh publishes empty snapshot, but does not inject runtime/languageService
    auto result = session.refresh();
    REQUIRE(result.succeeded);
    REQUIRE(session.snapshot() != nullptr);
    REQUIRE(session.runtime() == nullptr);         // still nullptr
    REQUIRE(session.languageService() == nullptr); // still nullptr
}

// ===========================================================================
// SES-014: default session (without refresh) calling createModelSet returns SessionError
//
// Contract (VoicebankSession.cpp:914-918): without snapshot, createModelSet first returns
// SessionError ("no snapshot available"), before findSinger/Runtime checks.
// Distinct from SES-004 (no Runtime after refresh -> InferenceNotInitialized) and SES-012
// (singer nonexistent after refresh -> SvsSingerNotFound).
// ===========================================================================
TEST_CASE("SES-014: createModelSet on default session without refresh returns SessionError",
          "[session][edge]") {
    ds::session::VoicebankSession session; // Default construction: not refreshed, snapshot nullptr
    REQUIRE(session.snapshot() == nullptr);

    // dummy ref: snapshot check precedes findSinger, ref content does not affect result
    ds::bank::SingerRef ref("any.pkg", "any_singer");
    auto exp = session.createModelSet(ref);
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::SessionError));
    REQUIRE(exp.error().message().find("no snapshot available") != std::string::npos);
}

// ===========================================================================
// SES-015: ModelSetHandle default construction
//
// ModelSetHandle has no public default constructor — the only constructor is
// private (ModelSetHandle.h:81-83) and callable solely by VoicebankSession via
// friend access. L1 therefore cannot instantiate an empty ModelSetHandle, nor
// is there a public factory. Verify the actual type contract instead: the class
// is not default-constructible, not copyable, not movable (copy is deleted and
// move is not implicitly declared), and only destructible. Empty-handle state
// queries are indirectly covered via createModelSet failure paths
// (see SES-004 / SES-014).
// ===========================================================================
TEST_CASE("SES-015: ModelSetHandle default construction", "[session][edge]") {
    using ds::session::ModelSetHandle;
    // Compile-time contract (also enforced at runtime for test visibility).
    static_assert(!std::is_default_constructible_v<ModelSetHandle>);
    static_assert(!std::is_copy_constructible_v<ModelSetHandle>);
    static_assert(!std::is_move_constructible_v<ModelSetHandle>);
    static_assert(std::is_destructible_v<ModelSetHandle>);

    REQUIRE_FALSE(std::is_default_constructible_v<ModelSetHandle>);
    REQUIRE_FALSE(std::is_copy_constructible_v<ModelSetHandle>);
    REQUIRE_FALSE(std::is_move_constructible_v<ModelSetHandle>);
    REQUIRE(std::is_destructible_v<ModelSetHandle>);
}

// ===========================================================================
// SES-016: setRoots replaced with empty roots, then refresh publishes empty snapshot
//
// Contract (VoicebankSession.cpp:528-532): after refresh, replacing roots with empty and refreshing again,
// because previous->roots != next->roots triggers changed=true, the new snapshot's
// singers/packages are empty. Verify the roots shrinking path does not crash and correctly publishes empty snapshot.
// ===========================================================================
TEST_CASE("SES-016: setRoots to empty after populated refresh publishes empty snapshot with changed=true",
          "[session][edge]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);
    REQUIRE_FALSE(session.snapshot()->singers.empty());

    // Replace with empty roots: roots difference triggers changed=true, scan result is empty
    session.setRoots({});
    const auto result = session.refresh();
    REQUIRE(result.succeeded);
    REQUIRE(result.changed);
    REQUIRE(result.snapshot != nullptr);
    REQUIRE(result.snapshot->singers.empty());
    REQUIRE(result.snapshot->packages.empty());
    REQUIRE(session.snapshot()->singers.empty());

    std::filesystem::remove_all(root);
}

// ===========================================================================
// SES-017: multiple refreshes on the same roots (no content change) return the same snapshot
//
// Contract (VoicebankSession.cpp:541-548): with no content change, refresh takes the no-op path,
// changed=false, returns previous snapshot (same pointer), generation does not increment.
// Distinct from SES-010 (content change -> generation increments, new snapshot object).
// ===========================================================================
TEST_CASE("SES-017: repeated refresh with unchanged roots returns same snapshot with changed=false",
          "[session][edge]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    // First refresh: previous=nullptr -> changed=true
    const auto first = session.refreshAsync().get();
    REQUIRE(first.succeeded);
    REQUIRE(first.changed);
    REQUIRE(first.snapshot != nullptr);
    const auto snap1 = session.snapshot();
    REQUIRE(snap1 != nullptr);
    const auto gen1 = snap1->generation;

    // Second refresh: same content -> no-op path, changed=false, snapshot pointer unchanged
    const auto second = session.refreshAsync().get();
    REQUIRE(second.succeeded);
    REQUIRE_FALSE(second.changed);
    REQUIRE(second.snapshot != nullptr);
    REQUIRE(second.snapshot == snap1);               // same snapshot object
    REQUIRE(session.snapshot() == snap1);            // session state unchanged
    REQUIRE(session.snapshot()->generation == gen1); // generation does not increment

    std::filesystem::remove_all(root);
}

// ===========================================================================
// SES-019: refresh returned snapshot points to the same object as session.snapshot()
//
// Contract (VoicebankSession.cpp:595-605): performRefresh publishes the new snapshot simultaneously
// to internal current and RefreshResult.snapshot, sharing ownership. Host can directly
// hold result.snapshot without additional query to session.snapshot(). Also verifies the no-op
// path (VoicebankSession.cpp:541-548) where both remain consistent.
// ===========================================================================
TEST_CASE("SES-019: refresh result snapshot identity matches session.snapshot()",
          "[session][edge]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    // Content-changing refresh: result.snapshot shares ownership with session.snapshot()
    const auto result = session.refreshAsync().get();
    REQUIRE(result.succeeded);
    REQUIRE(result.changed);
    REQUIRE(result.snapshot != nullptr);
    REQUIRE(result.snapshot == session.snapshot());

    // Second no-op refresh: result.snapshot still consistent with session.snapshot()
    const auto result2 = session.refreshAsync().get();
    REQUIRE(result2.succeeded);
    REQUIRE_FALSE(result2.changed);
    REQUIRE(result2.snapshot == session.snapshot());

    std::filesystem::remove_all(root);
}

// ===========================================================================
// SES-020: default session (without refresh) calling ensureLanguageReady returns SessionError
//
// Contract (VoicebankSession.cpp:989-993): without snapshot, ensureLanguageReady first returns
// SessionError ("no snapshot available"), before (packageId, version) lookup.
// Distinct from SES-009 (no Runtime after refresh -> RuntimePackageNotLoaded).
// ===========================================================================
TEST_CASE("SES-020: ensureLanguageReady on default session without refresh returns SessionError",
          "[session][edge]") {
    ds::session::VoicebankSession session; // Default construction: not refreshed, snapshot nullptr
    REQUIRE(session.snapshot() == nullptr);

    const auto version = stdc::VersionNumber::fromString("1.0.0").value();
    auto exp = session.ensureLanguageReady("any.pkg", version, "cmn");
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::SessionError));
    REQUIRE(exp.error().message().find("no snapshot available") != std::string::npos);
}
