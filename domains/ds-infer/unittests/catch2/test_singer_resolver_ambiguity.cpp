// Unit tests for SingerStageResolver ambiguity resolution and BF-23 regression.
//
// Covers:
//   a. resolve(nullptr) → InvalidArgument (directly testable).
//   b. Singer not found → SvsSingerNotFound (integration, now executable).
//   c. Ambiguous singers (same singerId, different packageId, no disambiguation)
//      → SvsSingerNotFound (integration, now executable).
//   d. Disambiguation by packageId succeeds (integration, now executable —
//      stage resolution fails due to no interpreter plugins, but the error
//      is NOT SvsSingerNotFound, proving disambiguation worked).
//   e. Stage resolution failures (InferenceStageMissing/SvsStageResolveFailed)
//      remain as [.integration] TODOs — they require interpreter plugins to
//      create import options, which are not available in unit tests.
//
// BF-23 path-based matching is no longer used — the current code matches by
// spec->packageId() string comparison. PackageId matching, multi-version
// isolation, and cross-package import declaration are covered by
// test_package_isolation.cpp.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>

#include <diffsinger/Infer/SingerStageResolver.h>

namespace fs = std::filesystem;
using namespace ds::infer;

namespace {

    fs::path makeTempRoot(const std::string &name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = fs::temp_directory_path() /
                   ("srt-resolver-" + name + "-" + std::to_string(stamp));
        fs::create_directories(dir);
        return dir;
    }

    void writeFile(const fs::path &path, const std::string &content) {
        fs::create_directories(path.parent_path());
        std::ofstream ofs(path);
        ofs << content;
    }

    // Create a minimal package with one inference + one singer importing it.
    fs::path createPackage(const fs::path &root,
                           const std::string &dirName,
                           const std::string &pkgId,
                           const std::string &version,
                           const std::string &singerId = "singer1") {
        auto pkgDir = root / dirName;
        fs::create_directories(pkgDir);

        writeFile(pkgDir / "pitch.json", "{\"id\": \"pitch\"}");
        writeFile(pkgDir / "singer.json",
                  "{\"id\": \"" + singerId + "\", \"level\": 1, "
                  "\"imports\": [{\"id\": \"pitch\"}]}");
        writeFile(pkgDir / "desc.json",
                  "{\n"
                  "  \"id\": \"" + pkgId + "\",\n"
                  "  \"version\": \"" + version + "\",\n"
                  "  \"contributes\": {\n"
                  "    \"inferences\": [\"pitch.json\"],\n"
                  "    \"singers\": [\"singer.json\"]\n"
                  "  }\n"
                  "}");

        return pkgDir;
    }

} // namespace

// ===========================================================================
// resolve(SingerSpec*) — null pointer handling (directly testable)
// ===========================================================================

TEST_CASE("SingerStageResolver resolve null singerSpec returns InvalidArgument",
          "[singer_resolver]") {
    SingerStageResolver resolver;
    auto exp = resolver.resolve(nullptr);
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.error().code() == srt::core::ErrorCode::InvalidArgument);
}

// ===========================================================================
// resolve(Runtime&, ...) — singer lookup (integration, now executable)
// ===========================================================================

TEST_CASE("SingerStageResolver non-existent singer returns SvsSingerNotFound",
          "[singer_resolver][integration]") {
    const auto root = makeTempRoot("not-found");
    createPackage(root, "pkg-a", "com.test.A", "1.0.0");

    srt::core::Runtime runtime;
    REQUIRE(runtime.loadPackage(root / "pkg-a").hasValue());

    SingerStageResolver resolver;
    auto exp = resolver.resolve(runtime, "com.test.A", "nonexistent");
    REQUIRE_FALSE(exp.hasValue());
    CHECK(exp.error().code() == srt::core::ErrorCode::SvsSingerNotFound);

    fs::remove_all(root);
}

TEST_CASE("SingerStageResolver empty singerId returns SvsSingerNotFound",
          "[singer_resolver][integration]") {
    const auto root = makeTempRoot("empty-id");
    createPackage(root, "pkg-a", "com.test.A", "1.0.0");

    srt::core::Runtime runtime;
    REQUIRE(runtime.loadPackage(root / "pkg-a").hasValue());

    SingerStageResolver resolver;
    auto exp = resolver.resolve(runtime, "com.test.A", "");
    REQUIRE_FALSE(exp.hasValue());
    CHECK(exp.error().code() == srt::core::ErrorCode::SvsSingerNotFound);

    fs::remove_all(root);
}

// ===========================================================================
// Ambiguity — multiple singers with same id, no disambiguation (BF-23)
// ===========================================================================

TEST_CASE("SingerStageResolver ambiguous singers without packageId returns SvsSingerNotFound",
          "[singer_resolver][bf-23][integration]") {
    const auto root = makeTempRoot("ambiguous");
    // Two packages with the same singerId "singer1" but different packageIds
    createPackage(root, "pkg-a", "com.test.A", "1.0.0");
    createPackage(root, "pkg-b", "com.test.B", "1.0.0");

    srt::core::Runtime runtime;
    REQUIRE(runtime.loadPackage(root / "pkg-a").hasValue());
    REQUIRE(runtime.loadPackage(root / "pkg-b").hasValue());

    SingerStageResolver resolver;
    // No packageId supplied → both candidates match → ambiguous
    auto exp = resolver.resolve(runtime, {}, "singer1");
    REQUIRE_FALSE(exp.hasValue());
    CHECK(exp.error().code() == srt::core::ErrorCode::SvsSingerNotFound);
    CHECK(exp.error().message().find("ambiguous") != std::string::npos);

    fs::remove_all(root);
}

TEST_CASE("SingerStageResolver disambiguates by packageId",
          "[singer_resolver][bf-23][integration]") {
    const auto root = makeTempRoot("disambig");
    // Two packages with the same singerId but different packageIds
    createPackage(root, "pkg-a", "com.test.A", "1.0.0");
    createPackage(root, "pkg-b", "com.test.B", "1.0.0");

    srt::core::Runtime runtime;
    REQUIRE(runtime.loadPackage(root / "pkg-a").hasValue());
    REQUIRE(runtime.loadPackage(root / "pkg-b").hasValue());

    SingerStageResolver resolver;
    // packageId="com.test.A" → selects A's singer (disambiguation succeeds)
    auto exp = resolver.resolve(runtime, "com.test.A", "singer1");
    REQUIRE_FALSE(exp.hasValue());
    // Disambiguation worked: error is NOT SvsSingerNotFound (ambiguous).
    // Stage resolution fails (no interpreter plugins → InferenceStageMissing
    // or SvsStageResolveFailed), but the key point is that the singer was
    // found and selected correctly.
    CHECK(exp.error().code() != srt::core::ErrorCode::SvsSingerNotFound);

    fs::remove_all(root);
}

TEST_CASE("SingerStageResolver disambiguates by version",
          "[singer_resolver][bf-23][integration]") {
    const auto root = makeTempRoot("disambig-ver");
    // Same packageId, two versions, both with singerId "singer1"
    createPackage(root, "pkg-v1", "com.test.A", "1.0.0");
    createPackage(root, "pkg-v2", "com.test.A", "2.0.0");

    srt::core::Runtime runtime;
    REQUIRE(runtime.loadPackage(root / "pkg-v1").hasValue());
    REQUIRE(runtime.loadPackage(root / "pkg-v2").hasValue());

    SingerStageResolver resolver;
    // Without version → ambiguous (2 candidates for same packageId)
    auto expAmbiguous = resolver.resolve(runtime, "com.test.A", "singer1");
    REQUIRE_FALSE(expAmbiguous.hasValue());
    CHECK(expAmbiguous.error().code() == srt::core::ErrorCode::SvsSingerNotFound);

    // With version="1.0.0" → selects v1 (disambiguation succeeds)
    auto expV1 = resolver.resolve(runtime, "com.test.A", "singer1", "1.0.0");
    REQUIRE_FALSE(expV1.hasValue());
    CHECK(expV1.error().code() != srt::core::ErrorCode::SvsSingerNotFound);

    fs::remove_all(root);
}

// ===========================================================================
// Stage resolution — requires interpreter plugins (integration)
// These remain as TODOs because creating ImportOptions requires an interpreter
// plugin, which is not available in unit tests.
// ===========================================================================

TEST_CASE("SingerStageResolver missing stage returns InferenceStageMissing",
          "[singer_resolver][.integration]") {
    // TODO: requires a singer with interpreter plugins loaded for 4 of 5
    //       stages. Without plugins, createImportOptions returns null and
    //       the resolver fails with SvsStageResolveFailed before reaching
    //       the InferenceStageMissing validation.
}

TEST_CASE("SingerStageResolver unresolved inference import returns SvsStageResolveFailed",
          "[singer_resolver][.integration]") {
    // TODO: requires a singer whose import.inference() is null. With the
    //       packageId injection fix (BF-29/BF-30), loadPackage rejects
    //       packages with unresolved same-package imports, so this scenario
    //       can only occur if the import was intentionally left unresolved
    //       (e.g. via a cross-package declaration that doesn't match).
}
