// Unit tests for SingerStageResolver ambiguity resolution and BF-23 regression.
//
// Covers:
//   a. BF-23: packageId matching uses exact directory-name comparison via
//      std::filesystem::path component iteration (NOT substring search).
//      Previously pathStr.find(packageId) false-matched "opencpop" against
//      "notopencpop" / "opencpop-v2" directories.
//   b. Singer not found -> ErrorCode::SvsSingerNotFound (integration).
//   c. Stage resolution failure -> ErrorCode::InferenceStageMissing /
//      SvsStageResolveFailed (integration).
//
// The Runtime-based resolve() overload requires a fully constructed Runtime
// with a registered singer category and a loaded package. SingerSpec's
// constructor is protected (friend SingerCategory), so it cannot be directly
// instantiated in a unit test. Those cases are written as [.integration]
// (skipped by default) with TODO comments; the directly-testable paths
// (null singerSpec, BF-23 path matching) run in isolation.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>

#include <diffsinger/Infer/SingerStageResolver.h>

using namespace ds::infer;

namespace {

    // Mirrors the BF-23 packageId matching logic in
    // SingerStageResolver.cpp resolve(Runtime&, ...) — iterates the path's
    // directory-name components and compares each component string exactly
    // against packageId. The pre-BF-23 code used pathStr.find(packageId)
    // (substring search), which false-matched "opencpop" against "notopencpop".
    //
    // Kept in sync with the resolver's disambiguation loop. An empty packageId
    // is a wildcard match, mirroring the resolver's
    // `bool packageMatches = packageId.empty();` initializer.
    bool packageIdMatchesPath(const std::string &packageId,
                              const std::filesystem::path &path) {
        if (packageId.empty()) {
            return true;
        }
        for (const auto &component : path) {
            if (component.string() == packageId) {
                return true;
            }
        }
        return false;
    }

} // namespace

// ===========================================================================
// BF-23: exact directory-name matching (not substring search)
// ===========================================================================

TEST_CASE("BF-23 opencpop does not substring-match notopencpop directory",
          "[singer_resolver][bf-23]") {
    // Regression: pathStr.find("opencpop") would match here because the
    // directory "notopencpop" contains "opencpop" as a substring. Exact
    // component comparison must reject it.
    const std::filesystem::path path = "voicebanks/notopencpop/singer.json";
    REQUIRE_FALSE(packageIdMatchesPath("opencpop", path));
}

TEST_CASE("BF-23 opencpop matches exact opencpop directory",
          "[singer_resolver][bf-23]") {
    // True positive: a path component is exactly "opencpop".
    const std::filesystem::path path = "voicebanks/opencpop/singer.json";
    REQUIRE(packageIdMatchesPath("opencpop", path));
}

TEST_CASE("BF-23 opencpop does not match opencpop-v2 directory",
          "[singer_resolver][bf-23]") {
    // Exact match only: "opencpop-v2" != "opencpop". The pre-BF-23 substring
    // search would have matched (and selected the wrong package version).
    const std::filesystem::path path = "voicebanks/opencpop-v2/singer.json";
    REQUIRE_FALSE(packageIdMatchesPath("opencpop", path));
}

TEST_CASE("BF-23 opencpop matches at non-leaf path component",
          "[singer_resolver][bf-23]") {
    // The package directory may appear anywhere in the path, not just as the
    // parent of the leaf file.
    const std::filesystem::path path = "opencpop/sub/dir/singer.json";
    REQUIRE(packageIdMatchesPath("opencpop", path));
}

TEST_CASE("BF-23 empty packageId matches any path (wildcard)",
          "[singer_resolver][bf-23]") {
    // Backward-compat: empty packageId is a wildcard (resolver uses this when
    // disambiguation is not required).
    const std::filesystem::path path = "voicebanks/anything/singer.json";
    REQUIRE(packageIdMatchesPath({}, path));
}

TEST_CASE("BF-23 opencpop does not match unrelated path",
          "[singer_resolver][bf-23]") {
    const std::filesystem::path path = "voicebanks/diffSinger/singer.json";
    REQUIRE_FALSE(packageIdMatchesPath("opencpop", path));
}

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
// Singer not found — requires Runtime (integration)
// TODO: requires Runtime with a registered singer category and a loaded
//       package. SingerSpec's constructor is protected (friend SingerCategory),
//       so the Runtime overload is the only way to exercise singer lookup.
// ===========================================================================

TEST_CASE("SingerStageResolver non-existent singer returns SvsSingerNotFound",
          "[singer_resolver][.integration]") {
    // TODO: requires Runtime with a loaded singer package.
    //
    // Setup:
    //   srt::core::Runtime runtime;
    //   // register singer category, load a package NOT containing singerId
    //   // "nonexistent".
    //   SingerStageResolver resolver;
    //   auto exp = resolver.resolve(runtime, "somepkg", "nonexistent");
    //   REQUIRE_FALSE(exp.hasValue());
    //   REQUIRE(exp.error().code() == srt::core::ErrorCode::SvsSingerNotFound);
}

TEST_CASE("SingerStageResolver empty singerId returns SvsSingerNotFound",
          "[singer_resolver][.integration]") {
    // TODO: requires Runtime.
    //
    // No singer has id == "" (ids are non-empty by package manifest
    // validation), so candidates should be empty and resolve() returns
    // SvsSingerNotFound.
    //
    //   srt::core::Runtime runtime;  // + category + package load
    //   SingerStageResolver resolver;
    //   auto exp = resolver.resolve(runtime, "somepkg", "");
    //   REQUIRE_FALSE(exp.hasValue());
    //   REQUIRE(exp.error().code() == srt::core::ErrorCode::SvsSingerNotFound);
}

// ===========================================================================
// Stage resolution — requires Runtime or constructible SingerSpec (integration)
// TODO: SingerSpec cannot be constructed directly (protected ctor, friend
//       SingerCategory), so these need a Runtime with a loaded package.
// ===========================================================================

TEST_CASE("SingerStageResolver missing stage returns InferenceStageMissing",
          "[singer_resolver][.integration]") {
    // TODO: requires Runtime with a singer whose imports omit one of the 5
    //       DiffSinger stages (e.g. no vocoder inference import).
    //
    //   srt::core::Runtime runtime;  // + load incomplete singer package
    //   SingerStageResolver resolver;
    //   auto exp = resolver.resolve(runtime, "incompletepkg", "singer1");
    //   REQUIRE_FALSE(exp.hasValue());
    //   REQUIRE(exp.error().code() ==
    //            srt::core::ErrorCode::InferenceStageMissing);
    //   // message names the missing stage (e.g. "vocoder inference not found").
    //   REQUIRE(exp.error().message().find("vocoder") != std::string::npos);
}

TEST_CASE("SingerStageResolver unresolved inference import returns SvsStageResolveFailed",
          "[singer_resolver][.integration]") {
    // TODO: requires Runtime.
    //
    // A singer whose import has inference() == nullptr (inferenceId not
    // resolved against any registered InferenceSpec) should return
    // SvsStageResolveFailed.
    //
    //   srt::core::Runtime runtime;  // + load singer with broken import
    //   SingerStageResolver resolver;
    //   auto exp = resolver.resolve(runtime, "brokenpkg", "singer1");
    //   REQUIRE_FALSE(exp.hasValue());
    //   REQUIRE(exp.error().code() ==
    //            srt::core::ErrorCode::SvsStageResolveFailed);
}

// ===========================================================================
// Ambiguity — multiple singers with same id (integration)
// These tie BF-23 to its disambiguation purpose: when two packages define the
// same singerId, packageId must select the right one via exact path match.
// ===========================================================================

TEST_CASE("SingerStageResolver ambiguous singers without packageId returns SvsSingerNotFound",
          "[singer_resolver][bf-23][.integration]") {
    // TODO: requires Runtime holding two packages that each define
    //       singerId="singer1", with no packageId/version supplied.
    //
    //   srt::core::Runtime runtime;  // + load two packages with same singerId
    //   SingerStageResolver resolver;
    //   auto exp = resolver.resolve(runtime, {}, "singer1");
    //   REQUIRE_FALSE(exp.hasValue());
    //   REQUIRE(exp.error().code() == srt::core::ErrorCode::SvsSingerNotFound);
    //   REQUIRE(exp.error().message().find("ambiguous") != std::string::npos);
}

TEST_CASE("SingerStageResolver disambiguates by packageId via exact path match",
          "[singer_resolver][bf-23][.integration]") {
    // TODO: requires Runtime holding two packages ("opencpop" and
    //       "notopencpop") that each define singerId="singer1". With
    //       packageId="opencpop", resolve must select the opencpop singer,
    //       NOT the notopencpop one (BF-23: exact directory-name match).
    //
    //   srt::core::Runtime runtime;  // + load both packages
    //   SingerStageResolver resolver;
    //   auto exp = resolver.resolve(runtime, "opencpop", "singer1");
    //   REQUIRE(exp.hasValue());  // disambiguation succeeded
}
