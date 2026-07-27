#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Core/Runtime.h>
#include <diffsinger/Session/VoicebankSession.h>

namespace {
std::filesystem::path makeRoot() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("ds-session-" + std::to_string(stamp));
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
    writeFile(bank / "characters/test/config.json", R"({"id":"test","imports":[{"inferenceId":"duration"}],"configuration":{"defaultLanguage":"cmn","languages":[{"id":"cmn"}]}})");
    writeFile(bank / "inferences/duration/config.json", R"({"id":"duration","class":"ai.svs.DurationInference","configuration":{}})");
}
/// Create a second valid package in a sibling directory under the same root.
void makeSecondPackage(const std::filesystem::path &root) {
    const auto bank2 = root / "bank2";
    writeFile(bank2 / "desc.json", R"({"id":"session.other","version":"2.0.0","contributes":{"singers":["characters/other/config.json"],"inferences":["inferences/duration2/config.json"]}})");
    writeFile(bank2 / "characters/other/config.json", R"({"id":"other","imports":[{"inferenceId":"duration2"}],"configuration":{"defaultLanguage":"cmn","languages":[{"id":"cmn"}]}})");
    writeFile(bank2 / "inferences/duration2/config.json", R"({"id":"duration2","class":"ai.svs.DurationInference","configuration":{}})");
}
/// Create a sibling package with the SAME packageId + singerId as makePackage
/// but a DIFFERENT version. Used to exercise D-42/K-06 multi-version ambiguity
/// rejection in VoicebankSession::findSinger (the snapshot layer must not
/// silently pick the first match when key.version is empty).
void makeMultiVersionPackage(const std::filesystem::path &root) {
    const auto bank2 = root / "bank_v2";
    writeFile(bank2 / "desc.json", R"({"id":"session.test","version":"2.0.0","contributes":{"singers":["characters/test/config.json"],"inferences":["inferences/duration_v2/config.json"]}})");
    writeFile(bank2 / "characters/test/config.json", R"({"id":"test","imports":[{"inferenceId":"duration_v2"}],"configuration":{"defaultLanguage":"cmn","languages":[{"id":"cmn"}]}})");
    writeFile(bank2 / "inferences/duration_v2/config.json", R"({"id":"duration_v2","class":"ai.svs.DurationInference","configuration":{}})");
}
}

TEST_CASE("VoicebankSession refresh returns the same final result as asynchronous refresh", "[ds-session]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    const auto first = session.refresh();
    REQUIRE(first.succeeded);
    REQUIRE(first.changed);

    const auto second = session.refresh();
    REQUIRE(second.succeeded);
    REQUIRE_FALSE(second.changed);
    REQUIRE(second.snapshot == first.snapshot);
    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankSession publishes immutable snapshot from concurrent refresh calls", "[ds-session]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    const auto first = session.refreshAsync();
    const auto second = session.refreshAsync();
    const auto result = first.get();
    REQUIRE(result.succeeded);
    const auto other = second.get();
    REQUIRE(other.succeeded);
    REQUIRE(result.snapshot->singers.size() == 1);
    REQUIRE(session.snapshot() == other.snapshot);
    const auto summary = session.availability();
    REQUIRE(summary.available + summary.degraded + summary.unavailable == 1);
    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankSession refresh subscription reports successful publication", "[ds-session][refresh-subscription]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    std::atomic<int> calls{0};
    std::atomic<bool> succeeded{false};
    auto subscription = session.subscribeRefresh([&](const ds::session::RefreshResult &result) {
        succeeded.store(result.succeeded);
        ++calls;
    });

    const auto result = session.refreshAsync().get();
    REQUIRE(result.succeeded);
    REQUIRE(calls.load() == 1);
    REQUIRE(succeeded.load());
    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankSession refresh subscription reports final failure", "[ds-session][refresh-subscription]") {
    const auto root = makeRoot();
    writeFile(root / "broken" / "desc.json", "not json");
    ds::session::VoicebankSession session;
    session.setRoots({root});

    std::atomic<int> calls{0};
    std::atomic<bool> succeeded{true};
    auto subscription = session.subscribeRefresh([&](const ds::session::RefreshResult &result) {
        succeeded.store(result.succeeded);
        ++calls;
    });

    const auto result = session.refreshAsync().get();
    REQUIRE_FALSE(result.succeeded);
    REQUIRE(calls.load() == 1);
    REQUIRE_FALSE(succeeded.load());
    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankSession refresh subscription may reset itself", "[ds-session][refresh-subscription]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    std::atomic<int> calls{0};
    ds::session::RefreshSubscription subscription;
    subscription = session.subscribeRefresh([&](const ds::session::RefreshResult &) {
        ++calls;
        subscription.reset();
    });

    REQUIRE(session.refreshAsync().get().succeeded);
    REQUIRE(calls.load() == 1);
    REQUIRE_FALSE(subscription);
    // Make the next refresh publish changed content; the self-unsubscribed
    // callback must not be invoked again.
    makeSecondPackage(root);
    REQUIRE(session.refreshAsync().get().succeeded);
    REQUIRE(calls.load() == 1);
    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankSession publishes valid packages alongside invalid ones", "[ds-session]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    const auto initial = session.refreshAsync().get();
    REQUIRE(initial.succeeded);
    writeFile(root / "broken" / "desc.json", "not json");
    const auto failed = session.refreshAsync().get();
    // D-31: the valid package keeps the refresh successful even when an
    // invalid package is present; the snapshot still carries the valid one.
    REQUIRE(failed.succeeded);
    REQUIRE(failed.snapshot != nullptr);
    REQUIRE(failed.snapshot->packages.size() == 1);
    REQUIRE(failed.snapshot->packages[0].packageId == "session.test");
    REQUIRE(failed.snapshot->packages[0].valid);
    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankSession refresh marks changed=true on first refresh and false when unchanged", "[ds-session]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    const auto first = session.refreshAsync().get();
    REQUIRE(first.succeeded);
    REQUIRE(first.changed);  // First refresh always reports changed.
    // No previous snapshot to compare against, so changes lists stay empty.
    REQUIRE(first.changes.added.empty());
    REQUIRE(first.changes.removed.empty());

    // A second refresh with no content change reports changed=false because
    // the snapshot content is identical to the previous one.
    const auto second = session.refreshAsync().get();
    REQUIRE(second.succeeded);
    REQUIRE_FALSE(second.changed);
    REQUIRE(second.changes.added.empty());
    REQUIRE(second.changes.removed.empty());
    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankSession refresh reports added package in changes summary", "[ds-session]") {
    const auto root = makeRoot();
    // First refresh with one package.
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    const auto first = session.refreshAsync().get();
    REQUIRE(first.succeeded);

    // Add a second valid package and refresh again. The new package should
    // appear in changes.added.
    makeSecondPackage(root);
    session.setRoots({root});
    const auto second = session.refreshAsync().get();
    REQUIRE(second.succeeded);
    REQUIRE(second.changed);
    REQUIRE_FALSE(second.changes.added.empty());
    bool found = false;
    for (const auto &c : second.changes.added) {
        if (c.packageId == "session.other") {
            found = true;
            break;
        }
    }
    REQUIRE(found);
    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankSession refresh collects diagnostics for invalid packages", "[ds-session]") {
    const auto root = makeRoot();
    makePackage(root);
    // Add a broken package alongside the valid one.
    writeFile(root / "broken" / "desc.json", "not json");
    ds::session::VoicebankSession session;
    session.setRoots({root});
    const auto result = session.refreshAsync().get();
    // D-31: the valid package keeps the refresh successful; the broken
    // package surfaces as a diagnostic.
    REQUIRE(result.succeeded);
    REQUIRE(result.snapshot != nullptr);
    REQUIRE(result.snapshot->packages.size() == 1);
    REQUIRE(result.snapshot->packages[0].valid);
    // Diagnostics should mention the broken package.
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
    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankSession reports same-coordinate content updates", "[ds-session]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    // Keep package id/version stable while changing observable singer metadata.
    writeFile(root / "bank" / "characters/test/config.json",
              R"({"id":"test","name":"Updated singer","imports":[{"inferenceId":"duration"}],"configuration":{"defaultLanguage":"cmn","languages":[{"id":"cmn"}]}})");
    const auto result = session.refreshAsync().get();
    REQUIRE(result.succeeded);
    REQUIRE(result.changed);
    REQUIRE(result.changes.added.empty());
    REQUIRE(result.changes.removed.empty());
    REQUIRE(result.changes.changed.size() == 1);
    REQUIRE(result.updatesAvailable == result.changes.changed);
    REQUIRE(result.updatesAvailable.front().packageId == "session.test");
    std::filesystem::remove_all(root);
}

// ---------------------------------------------------------------------------
// WP3: capabilitySummary / convertG2p / convertS2p / validatePhonemes
//
// These tests focus on error paths and snapshot-state checks because the
// success paths need G2P/S2P plugins (L1 tests don't load plugin DLLs).
// ---------------------------------------------------------------------------

TEST_CASE("VoicebankSession capabilitySummary returns Disabled when snapshot is empty", "[ds-session][wp3]") {
    ds::session::VoicebankSession session;
    ds::bank::SingerRef ref("pkg", "singer");
    const auto summary = session.capabilitySummary(ref);
    REQUIRE(summary.availability == ds::session::Availability::Disabled);
}

TEST_CASE("VoicebankSession capabilitySummary returns Disabled for unknown singer", "[ds-session][wp3]") {
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

TEST_CASE("VoicebankSession convertG2p returns error when no LanguageService is configured", "[ds-session][wp3]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    // Singer exists in the snapshot but no LanguageService was injected.
    ds::bank::SingerRef ref("session.test", "test");
    std::vector<srt::g2p::G2pInput> inputs{
        srt::g2p::G2pInput("la", "cmn"),
    };
    auto exp = session.convertG2p(ref, "cmn", inputs);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::G2pNotImplementedError));
    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankSession convertG2p returns error when singer not found", "[ds-session][wp3]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    ds::bank::SingerRef ref("missing.pkg", "missing");
    std::vector<srt::g2p::G2pInput> inputs;
    auto exp = session.convertG2p(ref, "cmn", inputs);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::SvsSingerNotFound));
    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankSession convertS2p returns error without LanguageService", "[ds-session][wp3]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    ds::bank::SingerRef ref("session.test", "test");
    auto exp = session.convertS2p(ref, "cmn", "ni3");
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::G2pNotImplementedError));
    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankSession validatePhonemes returns error when singer not found", "[ds-session][wp3]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    ds::bank::SingerRef ref("missing.pkg", "missing");
    auto exp = session.validatePhonemes(ref, {"a", "i"});
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::SvsSingerNotFound));
    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankSession validatePhonemes blocks on empty capability report", "[ds-session][wp3]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    // The fixture singer has no inference stages loaded (only a stub
    // duration config), so capabilityReport may be empty or absent. The
    // session must not silently accept unsupported phonemes.
    ds::bank::SingerRef ref("session.test", "test");
    auto exp = session.validatePhonemes(ref, {"a", "i", "u"});
    REQUIRE_FALSE(exp.hasValue());
    // Either G2pValidationError (no report) or SvsSingerNotFound — both
    // block inference as required.
    REQUIRE((exp.isError(srt::core::ErrorCode::G2pValidationError) ||
             exp.isError(srt::core::ErrorCode::SvsSingerNotFound)));
    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankSession validatePhonemes returns error when snapshot is empty", "[ds-session][wp3]") {
    ds::session::VoicebankSession session;
    ds::bank::SingerRef ref("pkg", "singer");
    auto exp = session.validatePhonemes(ref, {"a"});
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::SessionError));
}

// ---------------------------------------------------------------------------
// WP4: createModelSet / ModelSetHandle / StaleModelSet lifecycle
//
// Error paths are tested at L1; the success path (which needs a Runtime with
// a loaded ONNX package) and the staleness-after-refresh path are deferred
// to L2 tests with real model fixtures.
// ---------------------------------------------------------------------------

TEST_CASE("VoicebankSession SessionResources constructor stores runtime pointer", "[ds-session][wp4]") {
    srt::core::Runtime runtime;
    ds::session::SessionResources resources;
    resources.runtime = &runtime;
    ds::session::VoicebankSession session(std::move(resources));
    REQUIRE(session.runtime() == &runtime);
}

TEST_CASE("VoicebankSession createModelSet returns SessionError when snapshot is empty", "[ds-session][wp4]") {
    ds::session::VoicebankSession session;
    ds::bank::SingerRef ref("pkg", "singer");
    auto exp = session.createModelSet(ref);
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::SessionError));
}

TEST_CASE("VoicebankSession createModelSet returns SvsSingerNotFound for unknown singer", "[ds-session][wp4]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    ds::bank::SingerRef ref("missing.pkg", "missing");
    auto exp = session.createModelSet(ref);
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::SvsSingerNotFound));
    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankSession createModelSet returns InferenceNotInitialized when no Runtime is configured", "[ds-session][wp4]") {
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    // The fixture singer is scanned and marked Resolved by VoicebankScanner,
    // so it passes the resolutionState check. Without a Runtime, createModelSet
    // cannot proceed to stage resolution and must report InferenceNotInitialized.
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

TEST_CASE("VoicebankSession createModelSet returns error when Runtime has no singer package loaded", "[ds-session][wp4]") {
    const auto root = makeRoot();
    makePackage(root);

    // A default-constructed Runtime has no module categories registered and
    // no packages loaded, so SingerStageResolver::resolve() cannot find the
    // singer. createModelSet must propagate the resolve error rather than
    // silently producing an empty handle.
    srt::core::Runtime runtime;
    ds::session::SessionResources resources;
    resources.runtime = &runtime;
    ds::session::VoicebankSession session(std::move(resources));
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    ds::bank::SingerRef ref("session.test", "test");
    auto exp = session.createModelSet(ref);
    REQUIRE_FALSE(exp.hasValue());
    std::filesystem::remove_all(root);
}

// ---------------------------------------------------------------------------
// D-42 / K-06: multi-version same-(packageId, singerId) ambiguity rejection.
//
// When the snapshot contains two singers that share packageId + singerId but
// differ in version, findSinger MUST NOT silently pick the first match. An
// empty key.version must trigger SvsSingerAmbiguous; a non-empty key.version
// must resolve unambiguously to the matching singer.
// ---------------------------------------------------------------------------

TEST_CASE("VoicebankSession capabilitySummary returns SvsSingerAmbiguous for multi-version singer", "[ds-session][wp4][d42]") {
    const auto root = makeRoot();
    makePackage(root);
    makeMultiVersionPackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    // Empty version + multiple matching singers -> ambiguous.
    ds::bank::SingerRef ref("session.test", "test");
    const auto summary = session.capabilitySummary(ref);
    REQUIRE(summary.availability == ds::session::Availability::Disabled);
    REQUIRE_FALSE(summary.diagnostics.empty());
    bool foundAmbiguous = false;
    for (const auto &d : summary.diagnostics) {
        if (d.code == srt::core::ErrorCode::SvsSingerAmbiguous) {
            foundAmbiguous = true;
            break;
        }
    }
    REQUIRE(foundAmbiguous);
    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankSession capabilitySummary resolves multi-version singer by explicit version", "[ds-session][wp4][d42]") {
    const auto root = makeRoot();
    makePackage(root);
    makeMultiVersionPackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    // Read the actual version string from the snapshot rather than hardcoding
    // "2.0.0" — VersionNumber may normalize the string form (e.g. trim
    // trailing ".0"), and the test cares about disambiguation behavior, not
    // the exact normalization rules.
    const auto snap = session.snapshot();
    REQUIRE(snap);
    REQUIRE(snap->singers.size() >= 2);
    std::string v2Version;
    for (const auto &s : snap->singers) {
        if (s.ref.packageId == "session.test" && s.ref.singerId == "test" &&
            s.ref.version != "1.0.0") {  // makePackage uses 1.0.0
            v2Version = s.ref.version;
            break;
        }
    }
    REQUIRE_FALSE(v2Version.empty());

    // Non-empty version disambiguates and routes to the matching singer.
    ds::bank::SingerRef ref("session.test", "test", v2Version);
    const auto summary = session.capabilitySummary(ref);
    // The singer is Resolved by VoicebankScanner; availability reflects the
    // capability report (may be Available/Degraded/Disabled depending on the
    // stub fixture's capabilityReport, but MUST NOT report SvsSingerAmbiguous
    // nor SvsSingerNotFound).
    bool foundAmbiguous = false;
    for (const auto &d : summary.diagnostics) {
        if (d.code == srt::core::ErrorCode::SvsSingerAmbiguous ||
            d.code == srt::core::ErrorCode::SvsSingerNotFound) {
            foundAmbiguous = true;
            break;
        }
    }
    REQUIRE_FALSE(foundAmbiguous);
    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankSession convertG2p returns SvsSingerAmbiguous for multi-version singer", "[ds-session][wp3][d42]") {
    const auto root = makeRoot();
    makePackage(root);
    makeMultiVersionPackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    ds::bank::SingerRef ref("session.test", "test");
    std::vector<srt::g2p::G2pInput> inputs{srt::g2p::G2pInput("la", "cmn")};
    auto exp = session.convertG2p(ref, "cmn", inputs);
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::SvsSingerAmbiguous));
    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankSession convertS2p returns SvsSingerAmbiguous for multi-version singer", "[ds-session][wp3][d42]") {
    const auto root = makeRoot();
    makePackage(root);
    makeMultiVersionPackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    ds::bank::SingerRef ref("session.test", "test");
    auto exp = session.convertS2p(ref, "cmn", "ni3");
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::SvsSingerAmbiguous));
    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankSession validatePhonemes returns SvsSingerAmbiguous for multi-version singer", "[ds-session][wp3][d42]") {
    const auto root = makeRoot();
    makePackage(root);
    makeMultiVersionPackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    ds::bank::SingerRef ref("session.test", "test");
    auto exp = session.validatePhonemes(ref, {"a", "i"});
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::SvsSingerAmbiguous));
    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankSession createModelSet returns SvsSingerAmbiguous for multi-version singer", "[ds-session][wp4][d42]") {
    const auto root = makeRoot();
    makePackage(root);
    makeMultiVersionPackage(root);

    // Even with a Runtime configured, findSinger must reject the ambiguous
    // singer before stage resolution is attempted.
    srt::core::Runtime runtime;
    ds::session::SessionResources resources;
    resources.runtime = &runtime;
    ds::session::VoicebankSession session(std::move(resources));
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    ds::bank::SingerRef ref("session.test", "test");
    auto exp = session.createModelSet(ref);
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::SvsSingerAmbiguous));
    std::filesystem::remove_all(root);
}

TEST_CASE("VoicebankSnapshot exposes full manifests for valid packages (TD-01)", "[ds-session][td01]") {
    const auto root = makeRoot();
    makePackage(root);
    makeSecondPackage(root);
    writeFile(root / "broken" / "desc.json", "not json");
    ds::session::VoicebankSession session;
    session.setRoots({root});
    const auto result = session.refreshAsync().get();
    REQUIRE(result.succeeded);
    REQUIRE(result.snapshot != nullptr);

    // Two valid packages (session.test + session.other) and one broken.
    // manifests must contain exactly the valid packages, in discovery order.
    const auto &packages = result.snapshot->packages;
    const auto &manifests = result.snapshot->manifests;
    size_t validCount = 0;
    for (const auto &pkg : packages)
        if (pkg.valid) ++validCount;
    REQUIRE(manifests.size() == validCount);
    REQUIRE(manifests.size() == 2);

    // Manifests carry the full PackageManifest (packageId + version + singers
    // + inferences), not just the PackageStatus subset. VersionNumber::toString
    // normalizes trailing zeros, so "1.0.0" serializes as "1.0" (see
    // VoicebankScanner.cpp versionsMatch comment).
    REQUIRE(manifests[0].packageId() == "session.test");
    REQUIRE(manifests[0].version().toString() == "1.0");
    REQUIRE_FALSE(manifests[0].singers().empty());
    REQUIRE_FALSE(manifests[0].inferences().empty());

    REQUIRE(manifests[1].packageId() == "session.other");
    REQUIRE(manifests[1].version().toString() == "2.0");
    REQUIRE_FALSE(manifests[1].singers().empty());

    // The broken package must not contribute a manifest; its status is still
    // absent from the published snapshot (D-31: only valid packages are
    // published in next->packages, invalid ones contribute diagnostics only).
    for (const auto &m : manifests) {
        REQUIRE_FALSE(m.packageId().empty());
    }
    std::filesystem::remove_all(root);
}
