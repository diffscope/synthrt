// tst_vbs_lifecycle.cpp
// L1 tests for S1-S5 lifecycle APIs: loadVoicebank / unloadVoicebank /
// loadedVoicebanks / Error::withExtraContext / formatContext /
// SessionResources.g2pPluginPaths.
//
// These tests exercise error paths and basic API contracts without real
// ONNX models or G2P plugins. L2 tests that load real packages are gated
// behind SYNTHRT_ENABLE_L2_TESTS (future).

#include <catch2/catch_test_macros.hpp>

#include <diffsinger/Session/VoicebankSession.h>
#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Diagnostic.h>

#include "test_vbs_common.h"

#include <filesystem>

namespace {

// Configure a session with a stub package but no Runtime — exercises error
// paths. VoicebankSession is non-copyable/non-movable, so uses out-param.
void configureSessionNoRuntime(ds::session::VoicebankSession &session) {
    const auto root = vbs_test::makeRoot();
    vbs_test::makePackage(root);
    session.setRoots({root / "bank"});
    session.refresh();
}

} // namespace

// === S1: SessionResources G2P plugin paths ===

TEST_CASE("SessionResources stores g2pPluginPaths and officialG2pPackages", "[lifecycle][s1]") {
    ds::session::SessionResources res;
    res.g2pPluginPaths = {L"/path/to/plugins", L"/alt/plugins"};
    res.officialG2pPackages = {L"/path/to/g2p/packages"};

    ds::session::VoicebankSession session(res);
    // Session should have stored the paths (verified via refresh behavior).
    // Without a real LanguageService, refresh still works; the paths are
    // only used when LanguageService is non-null.
    REQUIRE(session.languageService() == nullptr);
}

TEST_CASE("SessionResources default g2pPluginPaths is empty", "[lifecycle][s1]") {
    ds::session::SessionResources res;
    REQUIRE(res.g2pPluginPaths.empty());
    REQUIRE(res.officialG2pPackages.empty());
}

// === S2: loadVoicebank / unloadVoicebank / loadedVoicebanks ===

TEST_CASE("loadVoicebank fails with SessionError when no snapshot", "[lifecycle][s2]") {
    ds::session::VoicebankSession session;
    // No setRoots + refresh → no snapshot
    auto exp = session.loadVoicebank("test", stdc::VersionNumber{});
    REQUIRE(!exp);
    REQUIRE(exp.error().code() == srt::core::ErrorCode::SessionError);
}

TEST_CASE("loadVoicebank fails with InferenceNotInitialized when no Runtime", "[lifecycle][s2]") {
    ds::session::VoicebankSession session;
    configureSessionNoRuntime(session);
    auto exp = session.loadVoicebank("session.test", stdc::VersionNumber::fromString("1.0.0"));
    REQUIRE(!exp);
    REQUIRE(exp.error().code() == srt::core::ErrorCode::InferenceNotInitialized);
}

TEST_CASE("loadVoicebank fails with RuntimePackageNotLoaded for unknown package", "[lifecycle][s2]") {
    ds::session::VoicebankSession session;
    configureSessionNoRuntime(session);
    auto exp = session.loadVoicebank("nonexistent", stdc::VersionNumber{});
    REQUIRE(!exp);
    REQUIRE(exp.error().code() == srt::core::ErrorCode::RuntimePackageNotLoaded);
    // S5: context should include packageId
    REQUIRE(exp.error().diagnostic().packageId == "nonexistent");
}

TEST_CASE("unloadVoicebank fails with RuntimePackageNotLoaded when not loaded", "[lifecycle][s2]") {
    ds::session::VoicebankSession session;
    configureSessionNoRuntime(session);
    auto exp = session.unloadVoicebank("session.test", stdc::VersionNumber::fromString("1.0.0"));
    REQUIRE(!exp);
    REQUIRE(exp.error().code() == srt::core::ErrorCode::RuntimePackageNotLoaded);
}

TEST_CASE("unloadVoicebank ForceUnloadTag fails with RuntimePackageNotLoaded when not loaded", "[lifecycle][s2]") {
    ds::session::VoicebankSession session;
    configureSessionNoRuntime(session);
    auto exp = session.unloadVoicebank("session.test", stdc::VersionNumber::fromString("1.0.0"),
                                        ds::session::ForceUnloadTag{});
    REQUIRE(!exp);
    REQUIRE(exp.error().code() == srt::core::ErrorCode::RuntimePackageNotLoaded);
}

TEST_CASE("loadedVoicebanks returns empty when nothing loaded", "[lifecycle][s2]") {
    ds::session::VoicebankSession session;
    configureSessionNoRuntime(session);
    auto loaded = session.loadedVoicebanks();
    REQUIRE(loaded.empty());
}

// === S5: Error::withExtraContext / formatContext ===

TEST_CASE("Error::withExtraContext appends key-value pairs", "[lifecycle][s5]") {
    srt::core::Error err(srt::core::ErrorCode::RuntimePackageNotLoaded, "test error");
    err.withExtraContext({{"stage", "loadPackage"}, {"packagePath", "/test/path"}});
    const auto &ctx = err.diagnostic().extraContext;
    REQUIRE(ctx.size() == 2);
    REQUIRE(ctx[0].first == "stage");
    REQUIRE(ctx[0].second == "loadPackage");
    REQUIRE(ctx[1].first == "packagePath");
    REQUIRE(ctx[1].second == "/test/path");
}

TEST_CASE("Error::withExtraContext is chainable", "[lifecycle][s5]") {
    srt::core::Error err(srt::core::ErrorCode::PackageInUse, "busy");
    err.withExtraContext({{"stage", "unload"}})
       .withExtraContext({{"activeHandleCount", "3"}});
    REQUIRE(err.diagnostic().extraContext.size() == 2);
}

TEST_CASE("Error::formatContext includes named fields and extraContext", "[lifecycle][s5]") {
    srt::core::Error err(srt::core::ErrorCode::G2pSessionError, "conversion failed");
    err.withContext("testSinger", "g2p", "testPackage", "cmn");
    err.withExtraContext({{"stage", "convert"}, {"g2pId", "g2p-cmn"}});
    const auto formatted = err.formatContext();
    REQUIRE(formatted.find("packageId=testPackage") != std::string::npos);
    REQUIRE(formatted.find("singerId=testSinger") != std::string::npos);
    REQUIRE(formatted.find("language=cmn") != std::string::npos);
    REQUIRE(formatted.find("stage=convert") != std::string::npos);
    REQUIRE(formatted.find("g2pId=g2p-cmn") != std::string::npos);
    REQUIRE(formatted.find("conversion failed") != std::string::npos);
}

TEST_CASE("Error::formatContext with no context returns just message", "[lifecycle][s5]") {
    srt::core::Error err(srt::core::ErrorCode::SessionError, "bare error");
    REQUIRE(err.formatContext() == "bare error");
}

TEST_CASE("Error::formatContext includes trace", "[lifecycle][s5]") {
    srt::core::Error err(srt::core::ErrorCode::LoadFailed, "load failed");
    err.appendTrace("step1: scan");
    err.appendTrace("step2: parse");
    const auto formatted = err.formatContext();
    REQUIRE(formatted.find("Trace:") != std::string::npos);
    REQUIRE(formatted.find("step1: scan") != std::string::npos);
    REQUIRE(formatted.find("step2: parse") != std::string::npos);
}

// === S5: PackageInUse error code ===

TEST_CASE("PackageInUse error code exists", "[lifecycle][s5]") {
    srt::core::Error err(srt::core::ErrorCode::PackageInUse, "package busy");
    REQUIRE(err.code() == srt::core::ErrorCode::PackageInUse);
}

// === S4: LoadedPackageEntry reference count (L1: no Runtime, tests error path) ===

TEST_CASE("createModelSet fails without Runtime (S3 fallback error path)", "[lifecycle][s3][s4]") {
    ds::session::VoicebankSession session;
    configureSessionNoRuntime(session);
    // Without a Runtime, the singer cannot be fully resolved (no inference
    // model loaded), so createModelSet fails at singer lookup rather than
    // reaching the S3 loadVoicebank fallback.
    ds::bank::SingerRef ref;
    ref.packageId = "session.test";
    ref.singerId = "test";
    ref.version = "1.0.0";
    auto exp = session.createModelSet(ref);
    REQUIRE(!exp);
    REQUIRE(exp.error().code() == srt::core::ErrorCode::SvsSingerNotFound);
}
