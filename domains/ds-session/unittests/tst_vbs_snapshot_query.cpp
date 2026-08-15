// A2 unit tests for VoicebankSnapshot query methods.
//
// Covers the contract documented in docs/lite-integration/02-synthrt-side-changes.md §A2:
//   A2-T01: findSinger(SingerRef{pkg, singer, "1.0.0"}) 精确匹配 → 命中
//   A2-T02: findSinger(SingerRef{pkg, singer, "1.0"}) 版本规范化 → 命中（与 "1.0.0" 等价）
//   A2-T03: findSinger(SingerRef{pkg, singer, "2.0.0"}) 不存在 → nullptr
//   A2-T04: findSingersBySingerId("singerA") 多版本场景 → 2 个
//   A2-T05: findSingersBySingerId("notExist") → 空 vector
//   A2-T06: findPackage(pkg, ver) valid package → 命中；valid==true
//   A2-T07: findPackage(pkg, ver) invalid package → 命中；valid==false（仍返回）
//   A2-T08: findPackage(pkg, ver) 不存在 → nullptr
//   A2-T09: findManifest(pkg, ver) valid package → 命中
//   A2-T10: findManifest(pkg, ver) invalid package → nullptr
//   A2-T11: 多线程并发 findSinger 读 snapshot → 无数据竞争
//
// These are pure L1 tests: they manually populate a VoicebankSnapshot struct
// and exercise the const query methods. No Runtime / ONNX runtime / plugin
// DLL is needed. The snapshot is immutable after publication, so the const
// methods are safe to call concurrently (A2-T11).

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <diffsinger/Bank/PackageManifest.h>
#include <diffsinger/Bank/PackageStatus.h>
#include <diffsinger/Bank/SingerRef.h>
#include <diffsinger/Bank/SingerSnapshot.h>
#include <diffsinger/Session/VoicebankSession.h>

using ds::bank::PackageManifest;
using ds::bank::PackageStatus;
using ds::bank::SingerRef;
using ds::bank::SingerSnapshot;
using ds::session::VoicebankSnapshot;
using stdc::VersionNumber;

namespace {

    // Build a snapshot with two versions of the same (packageId, singerId)
    // plus one invalid package (no manifest) plus one extra singer with a
    // different singerId. Used by most A2 test cases.
    VoicebankSnapshot makeSampleSnapshot() {
        VoicebankSnapshot snap;

        // Singer A v1.0.0
        {
            SingerSnapshot s;
            s.ref = SingerRef("pkg.alpha", "singerA", "1.0.0");
            s.name = "Singer A v1";
            s.version = "1.0.0";
            snap.singers.push_back(std::move(s));
        }
        // Singer A v2.0.0 (same packageId + singerId, different version)
        {
            SingerSnapshot s;
            s.ref = SingerRef("pkg.alpha", "singerA", "2.0.0");
            s.name = "Singer A v2";
            s.version = "2.0.0";
            snap.singers.push_back(std::move(s));
        }
        // Singer B (different singerId, single version)
        {
            SingerSnapshot s;
            s.ref = SingerRef("pkg.beta", "singerB", "1.0.0");
            s.name = "Singer B";
            s.version = "1.0.0";
            snap.singers.push_back(std::move(s));
        }

        // Package pkg.alpha v1.0.0 (valid)
        {
            PackageStatus p;
            p.packageId = "pkg.alpha";
            p.version = VersionNumber::fromString("1.0.0").value();
            p.rootPath = "voicebanks/alpha/v1";
            p.valid = true;
            snap.packages.push_back(std::move(p));
        }
        // Package pkg.alpha v2.0.0 (valid)
        {
            PackageStatus p;
            p.packageId = "pkg.alpha";
            p.version = VersionNumber::fromString("2.0.0").value();
            p.rootPath = "voicebanks/alpha/v2";
            p.valid = true;
            snap.packages.push_back(std::move(p));
        }
        // Package pkg.beta v1.0.0 (valid)
        {
            PackageStatus p;
            p.packageId = "pkg.beta";
            p.version = VersionNumber::fromString("1.0.0").value();
            p.rootPath = "voicebanks/beta";
            p.valid = true;
            snap.packages.push_back(std::move(p));
        }
        // Package pkg.gamma v1.0.0 (invalid — corrupt desc.json)
        {
            PackageStatus p;
            p.packageId = "pkg.gamma";
            p.version = VersionNumber::fromString("1.0.0").value();
            p.rootPath = "voicebanks/gamma";
            p.valid = false;
            snap.packages.push_back(std::move(p));
        }

        // Manifests: valid packages only (TD-01). pkg.gamma is invalid and
        // therefore has no manifest entry.
        {
            PackageManifest m;
            m.setPackageId("pkg.alpha");
            m.setVersion(VersionNumber::fromString("1.0.0").value());
            snap.manifests.push_back(std::move(m));
        }
        {
            PackageManifest m;
            m.setPackageId("pkg.alpha");
            m.setVersion(VersionNumber::fromString("2.0.0").value());
            snap.manifests.push_back(std::move(m));
        }
        {
            PackageManifest m;
            m.setPackageId("pkg.beta");
            m.setVersion(VersionNumber::fromString("1.0.0").value());
            snap.manifests.push_back(std::move(m));
        }

        return snap;
    }

} // namespace

// ===========================================================================
// A2-T01: findSinger exact version match
// ===========================================================================

TEST_CASE("A2-T01: findSinger with exact version match returns SingerSnapshot",
          "[ds-session][snapshot-query][a2]") {
    const auto snap = makeSampleSnapshot();

    const auto *s = snap.findSinger(SingerRef("pkg.alpha", "singerA", "1.0.0"));
    REQUIRE(s != nullptr);
    REQUIRE(s->ref.packageId == "pkg.alpha");
    REQUIRE(s->ref.singerId == "singerA");
    REQUIRE(s->ref.version == "1.0.0");
    REQUIRE(s->name == "Singer A v1");
}

// ===========================================================================
// A2-T02: findSinger with version normalization ("1.0" matches "1.0.0")
//
// VersionNumber::fromString normalizes "1.0" and "1.0.0" to the same internal
// representation, so findSinger must treat them as equal. This is the
// contract documented in project_memory: "'1.0' == '1.0.0' == '1.0.0.0'".
// ===========================================================================

TEST_CASE("A2-T02: findSinger normalizes version strings (1.0 == 1.0.0)",
          "[ds-session][snapshot-query][a2]") {
    const auto snap = makeSampleSnapshot();

    // "1.0" stored snapshot has "1.0.0"; query with "1.0" must match.
    const auto *s = snap.findSinger(SingerRef("pkg.alpha", "singerA", "1.0"));
    REQUIRE(s != nullptr);
    REQUIRE(s->ref.version == "1.0.0");

    // Also verify the reverse: snapshot stores "1.0", query with "1.0.0".
    // Build a snapshot where the stored version is the short form.
    VoicebankSnapshot snap2;
    SingerSnapshot ss;
    ss.ref = SingerRef("pkg.short", "singerShort", "1.0");
    snap2.singers.push_back(std::move(ss));

    const auto *r = snap2.findSinger(SingerRef("pkg.short", "singerShort", "1.0.0"));
    REQUIRE(r != nullptr);
    REQUIRE(r->ref.version == "1.0");
}

// ===========================================================================
// A2-T03: findSinger with non-existent version returns nullptr
// ===========================================================================

TEST_CASE("A2-T03: findSinger with unknown version returns nullptr",
          "[ds-session][snapshot-query][a2]") {
    const auto snap = makeSampleSnapshot();

    const auto *s = snap.findSinger(SingerRef("pkg.alpha", "singerA", "9.9.9"));
    REQUIRE(s == nullptr);
}

// ===========================================================================
// A2-T04: findSingersBySingerId returns multiple entries for multi-version
// ===========================================================================

TEST_CASE("A2-T04: findSingersBySingerId returns multiple for multi-version",
          "[ds-session][snapshot-query][a2]") {
    const auto snap = makeSampleSnapshot();

    const auto matches = snap.findSingersBySingerId("singerA");
    REQUIRE(matches.size() == 2);
    // Both matches must share the same singerId
    for (const auto *m : matches) {
        REQUIRE(m->ref.singerId == "singerA");
    }
    // Versions must be distinct (1.0.0 and 2.0.0)
    REQUIRE(((matches[0]->ref.version == "1.0.0" && matches[1]->ref.version == "2.0.0") ||
             (matches[0]->ref.version == "2.0.0" && matches[1]->ref.version == "1.0.0")));
}

// ===========================================================================
// A2-T05: findSingersBySingerId with unknown singerId returns empty vector
// ===========================================================================

TEST_CASE("A2-T05: findSingersBySingerId returns empty for unknown singerId",
          "[ds-session][snapshot-query][a2]") {
    const auto snap = makeSampleSnapshot();

    const auto matches = snap.findSingersBySingerId("notExist");
    REQUIRE(matches.empty());
}

// ===========================================================================
// A2-T06: findPackage with valid package returns PackageStatus (valid==true)
// ===========================================================================

TEST_CASE("A2-T06: findPackage returns valid PackageStatus",
          "[ds-session][snapshot-query][a2]") {
    const auto snap = makeSampleSnapshot();

    const auto *p = snap.findPackage("pkg.alpha", VersionNumber::fromString("1.0.0").value());
    REQUIRE(p != nullptr);
    REQUIRE(p->packageId == "pkg.alpha");
    REQUIRE(p->valid == true);
}

// ===========================================================================
// A2-T07: findPackage with invalid package returns PackageStatus (valid==false)
//
// Invalid packages remain in `packages` with valid=false so callers can report
// the parse error to the user. findPackage must return them too — callers
// check the `valid` field themselves.
// ===========================================================================

TEST_CASE("A2-T07: findPackage returns invalid PackageStatus (still present)",
          "[ds-session][snapshot-query][a2]") {
    const auto snap = makeSampleSnapshot();

    const auto *p = snap.findPackage("pkg.gamma", VersionNumber::fromString("1.0.0").value());
    REQUIRE(p != nullptr);
    REQUIRE(p->packageId == "pkg.gamma");
    REQUIRE(p->valid == false);
}

// ===========================================================================
// A2-T08: findPackage with unknown package returns nullptr
// ===========================================================================

TEST_CASE("A2-T08: findPackage with unknown package returns nullptr",
          "[ds-session][snapshot-query][a2]") {
    const auto snap = makeSampleSnapshot();

    const auto *p = snap.findPackage("nonexistent.pkg", VersionNumber::fromString("1.0.0").value());
    REQUIRE(p == nullptr);
}

// ===========================================================================
// A2-T09: findManifest with valid package returns PackageManifest
// ===========================================================================

TEST_CASE("A2-T09: findManifest returns PackageManifest for valid package",
          "[ds-session][snapshot-query][a2]") {
    const auto snap = makeSampleSnapshot();

    const auto *m = snap.findManifest("pkg.alpha", VersionNumber::fromString("1.0.0").value());
    REQUIRE(m != nullptr);
    REQUIRE(m->packageId() == "pkg.alpha");
}

// ===========================================================================
// A2-T10: findManifest with invalid package returns nullptr (TD-01)
//
// Invalid packages have no manifest entry — they only contribute their
// PackageStatus.error to the snapshot. findManifest must return nullptr so
// callers can branch on validity without inspecting PackageStatus first.
// ===========================================================================

TEST_CASE("A2-T10: findManifest returns nullptr for invalid package",
          "[ds-session][snapshot-query][a2]") {
    const auto snap = makeSampleSnapshot();

    const auto *m = snap.findManifest("pkg.gamma", VersionNumber::fromString("1.0.0").value());
    REQUIRE(m == nullptr);
}

// ===========================================================================
// A2-T11: concurrent findSinger reads are data-race-free
//
// VoicebankSnapshot is immutable after publication (VoicebankSession publishes
// it via shared_ptr<const VoicebankSnapshot>). The const query methods only
// read internal vectors, so multiple threads can call them concurrently
// without synchronization. This test exercises that contract by spawning
// N threads that each perform findSinger/findPackage/findManifest in a loop
// against the same snapshot.
// ===========================================================================

TEST_CASE("A2-T11: concurrent findSinger reads are data-race-free",
          "[ds-session][snapshot-query][a2][concurrent]") {
    const auto snap = makeSampleSnapshot();

    constexpr int kThreads = 4;
    constexpr int kIterations = 1000;

    std::atomic<int> successCount{0};
    std::atomic<int> nullCount{0};

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&snap, &successCount, &nullCount, t]() {
            const auto ref = SingerRef("pkg.alpha", "singerA", "1.0.0");
            const auto ver = VersionNumber::fromString("1.0.0").value();
            for (int i = 0; i < kIterations; ++i) {
                // Alternate between findSinger / findPackage / findManifest
                // so all three methods are exercised concurrently.
                switch ((t + i) % 3) {
                    case 0: {
                        const auto *s = snap.findSinger(ref);
                        if (s) successCount.fetch_add(1, std::memory_order_relaxed);
                        else    nullCount.fetch_add(1, std::memory_order_relaxed);
                        break;
                    }
                    case 1: {
                        const auto *p = snap.findPackage("pkg.alpha", ver);
                        if (p) successCount.fetch_add(1, std::memory_order_relaxed);
                        else    nullCount.fetch_add(1, std::memory_order_relaxed);
                        break;
                    }
                    case 2: {
                        const auto *m = snap.findManifest("pkg.alpha", ver);
                        if (m) successCount.fetch_add(1, std::memory_order_relaxed);
                        else    nullCount.fetch_add(1, std::memory_order_relaxed);
                        break;
                    }
                }
            }
        });
    }

    for (auto &th : threads) th.join();

    // All successful queries must have found their target — there are no
    // nullptr results expected in this scenario.
    REQUIRE(successCount.load() == kThreads * kIterations);
    REQUIRE(nullCount.load() == 0);
}
