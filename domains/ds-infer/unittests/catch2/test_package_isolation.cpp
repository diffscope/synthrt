// Unit tests for Runtime package isolation: multi-version coexistence,
// duplicate detection (BF-29), and cross-package import declaration (BF-30).
//
// These tests create minimal voice bank packages on disk and load them via
// Runtime::loadPackage to verify:
//   - Same packageId with different versions can coexist (multi-version)
//   - Same packageId + version is rejected with ErrorCode::PackageDuplicate
//   - Same-package imports resolve strictly (no cross-package leakage)
//   - Cross-package imports resolve when explicitly declared (ARCH-06)
//   - Cross-package wildcard version ("*") matches any version
//   - Cross-package exact version matches only the declared version
//   - Cross-package import to nonexistent package fails with error

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
#include <synthrt/SVS/SingerContrib.h>

namespace fs = std::filesystem;

namespace {

    fs::path makeTempRoot(const std::string &name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = fs::temp_directory_path() /
                   ("srt-iso-" + name + "-" + std::to_string(stamp));
        fs::create_directories(dir);
        return dir;
    }

    void writeFile(const fs::path &path, const std::string &content) {
        fs::create_directories(path.parent_path());
        std::ofstream ofs(path);
        ofs << content;
    }

    // Create a minimal package directory with desc.json + inference configs
    // + optional singer config. Each inferenceId gets a config file
    // "<id>.json" with {"id":"<id>"} (no class — interpreter plugin won't
    // be found, but loadSpec gracefully skips schema creation).
    // If singerImportsJson is non-empty, "singer.json" is created with the
    // given imports JSON array.
    fs::path createPackage(const fs::path &root,
                           const std::string &dirName,
                           const std::string &pkgId,
                           const std::string &version,
                           const std::vector<std::string> &inferenceIds,
                           const std::string &singerImportsJson = "") {
        auto pkgDir = root / dirName;
        fs::create_directories(pkgDir);

        // Inference configs + contributes.inferences array
        std::string infArray = "[";
        bool first = true;
        for (const auto &id : inferenceIds) {
            std::string filename = id + ".json";
            writeFile(pkgDir / filename, "{\"id\": \"" + id + "\"}");
            if (!first) infArray += ", ";
            infArray += "\"" + filename + "\"";
            first = false;
        }
        infArray += "]";

        // Singer config + contributes.singers array
        std::string singerArray = "[]";
        if (!singerImportsJson.empty()) {
            writeFile(pkgDir / "singer.json",
                      "{\"id\": \"singer1\", \"level\": 1, \"imports\": " +
                          singerImportsJson + "}");
            singerArray = "[\"singer.json\"]";
        }

        // desc.json
        writeFile(pkgDir / "desc.json",
                  "{\n"
                  "  \"id\": \"" + pkgId + "\",\n"
                  "  \"version\": \"" + version + "\",\n"
                  "  \"contributes\": {\n"
                  "    \"inferences\": " + infArray + ",\n"
                  "    \"singers\": " + singerArray + "\n"
                  "  }\n"
                  "}");

        return pkgDir;
    }

} // namespace

// ===========================================================================
// Multi-version coexistence: same packageId, different versions
// ===========================================================================

TEST_CASE("Multi-version: same packageId different versions coexist",
          "[package][isolation][multi-version]") {
    const auto root = makeTempRoot("multi-version");
    createPackage(root, "pkg-v1", "com.test.pkg", "1.0.0", {"pitch"});
    createPackage(root, "pkg-v2", "com.test.pkg", "2.0.0", {"pitch"});

    srt::core::Runtime runtime;
    REQUIRE(runtime.loadPackage(root / "pkg-v1").hasValue());
    REQUIRE(runtime.loadPackage(root / "pkg-v2").hasValue());

    // Both versions should have their inference specs registered
    auto *infCat = runtime.moduleCategory("inference");
    REQUIRE(infCat != nullptr);
    auto specs = infCat->specs();
    CHECK(specs.size() == 2);

    // Verify same id, same packageId, but different versions
    int v1Count = 0, v2Count = 0;
    for (auto *spec : specs) {
        CHECK(spec->id() == "pitch");
        CHECK(spec->packageId() == "com.test.pkg");
        if (spec->packageVersion().toString() == "1.0") v1Count++;
        if (spec->packageVersion().toString() == "2.0") v2Count++;
    }
    CHECK(v1Count == 1);
    CHECK(v2Count == 1);

    fs::remove_all(root);
}

// ===========================================================================
// Duplicate detection: same packageId + version is rejected (BF-29)
// ===========================================================================

TEST_CASE("Duplicate: same packageId+version rejected (inference)",
          "[package][isolation][duplicate][bf-29]") {
    const auto root = makeTempRoot("dup-inf");
    createPackage(root, "pkg-a", "com.test.pkg", "1.0.0", {"pitch"});
    createPackage(root, "pkg-b", "com.test.pkg", "1.0.0", {"pitch"});

    srt::core::Runtime runtime;
    REQUIRE(runtime.loadPackage(root / "pkg-a").hasValue());

    auto result = runtime.loadPackage(root / "pkg-b");
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().code() == srt::core::ErrorCode::PackageDuplicate);

    fs::remove_all(root);
}

TEST_CASE("Duplicate: same packageId+version rejected (singer)",
          "[package][isolation][duplicate][bf-29]") {
    const auto root = makeTempRoot("dup-singer");
    // Both packages have singer "singer1" with the same packageId+version.
    // Inference "pitch" also matches, but inference duplicate is detected
    // first (inferences load before singers).
    createPackage(root, "pkg-a", "com.test.pkg", "1.0.0", {"pitch"},
                  "[{\"id\": \"pitch\"}]");
    createPackage(root, "pkg-b", "com.test.pkg", "1.0.0", {"pitch"},
                  "[{\"id\": \"pitch\"}]");

    srt::core::Runtime runtime;
    REQUIRE(runtime.loadPackage(root / "pkg-a").hasValue());

    auto result = runtime.loadPackage(root / "pkg-b");
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().code() == srt::core::ErrorCode::PackageDuplicate);

    fs::remove_all(root);
}

// ===========================================================================
// Cross-package isolation: default same-package import (no leakage)
// ===========================================================================

TEST_CASE("Isolation: same-package import does not leak across packages",
          "[package][isolation][cross-package]") {
    const auto root = makeTempRoot("isolation");
    // Package A: has "pitch" inference + singer importing "pitch"
    createPackage(root, "pkg-a", "com.test.A", "1.0.0", {"pitch"},
                  "[{\"id\": \"pitch\"}]");
    // Package B: has "pitch" inference (same id, different package)
    createPackage(root, "pkg-b", "com.test.B", "1.0.0", {"pitch"});

    srt::core::Runtime runtime;
    REQUIRE(runtime.loadPackage(root / "pkg-a").hasValue());
    REQUIRE(runtime.loadPackage(root / "pkg-b").hasValue());

    // Verify A's singer resolves to A's pitch, not B's
    auto *singerCat = runtime.moduleCategory("singer");
    REQUIRE(singerCat != nullptr);
    auto singers = static_cast<srt::svs::SingerCategory *>(singerCat)->singers();
    REQUIRE(singers.size() == 1);
    CHECK(singers[0]->id() == "singer1");
    CHECK(singers[0]->packageId() == "com.test.A");

    const auto &imports = singers[0]->imports();
    REQUIRE(imports.size() == 1);
    REQUIRE(imports[0].inference() != nullptr);
    CHECK(imports[0].inference()->packageId() == "com.test.A");
    CHECK(imports[0].inference()->packageVersion().toString() == "1.0");

    fs::remove_all(root);
}

// ===========================================================================
// Cross-package import declaration (ARCH-06, BF-30)
// ===========================================================================

TEST_CASE("Cross-package: explicit declaration resolves to other package",
          "[package][isolation][cross-package][bf-30]") {
    const auto root = makeTempRoot("crosspkg");
    // Package B: provides "vocoder" inference
    createPackage(root, "pkg-b", "com.test.B", "1.0.0", {"vocoder"});
    // Package A: singer imports "pitch" (same-package) + "vocoder" (cross-package)
    createPackage(root, "pkg-a", "com.test.A", "1.0.0", {"pitch"},
                  "[{\"id\": \"pitch\"}, "
                  "{\"id\": \"vocoder\", \"package\": \"com.test.B\", \"version\": \"*\"}]");

    srt::core::Runtime runtime;
    // Load B first (provides vocoder), then A (imports vocoder from B)
    REQUIRE(runtime.loadPackage(root / "pkg-b").hasValue());
    REQUIRE(runtime.loadPackage(root / "pkg-a").hasValue());

    auto *singerCat = runtime.moduleCategory("singer");
    REQUIRE(singerCat != nullptr);
    auto singers = static_cast<srt::svs::SingerCategory *>(singerCat)->singers();
    REQUIRE(singers.size() == 1);

    const auto &imports = singers[0]->imports();
    REQUIRE(imports.size() == 2);
    // First import: same-package "pitch"
    REQUIRE(imports[0].inference() != nullptr);
    CHECK(imports[0].inference()->id() == "pitch");
    CHECK(imports[0].inference()->packageId() == "com.test.A");
    // Second import: cross-package "vocoder" from B
    REQUIRE(imports[1].inference() != nullptr);
    CHECK(imports[1].inference()->id() == "vocoder");
    CHECK(imports[1].inference()->packageId() == "com.test.B");

    fs::remove_all(root);
}

TEST_CASE("Cross-package: exact version match resolves correctly",
          "[package][isolation][cross-package][bf-30]") {
    const auto root = makeTempRoot("crosspkg-ver");
    // Package B v1.0.0 and v2.0.0: both provide "vocoder"
    createPackage(root, "pkg-b-v1", "com.test.B", "1.0.0", {"vocoder"});
    createPackage(root, "pkg-b-v2", "com.test.B", "2.0.0", {"vocoder"});
    // Package A: singer imports "vocoder" from B v1.0.0 (exact match)
    createPackage(root, "pkg-a", "com.test.A", "1.0.0", {"pitch"},
                  "[{\"id\": \"pitch\"}, "
                  "{\"id\": \"vocoder\", \"package\": \"com.test.B\", \"version\": \"1.0.0\"}]");

    srt::core::Runtime runtime;
    REQUIRE(runtime.loadPackage(root / "pkg-b-v1").hasValue());
    REQUIRE(runtime.loadPackage(root / "pkg-b-v2").hasValue());
    REQUIRE(runtime.loadPackage(root / "pkg-a").hasValue());

    auto *singerCat = runtime.moduleCategory("singer");
    auto singers = static_cast<srt::svs::SingerCategory *>(singerCat)->singers();
    REQUIRE(singers.size() == 1);

    const auto &imports = singers[0]->imports();
    REQUIRE(imports.size() == 2);
    // Cross-package import should resolve to B v1.0.0, not v2.0.0
    REQUIRE(imports[1].inference() != nullptr);
    CHECK(imports[1].inference()->packageId() == "com.test.B");
    CHECK(imports[1].inference()->packageVersion().toString() == "1.0");

    fs::remove_all(root);
}

TEST_CASE("Cross-package: import to nonexistent package fails",
          "[package][isolation][cross-package][bf-30]") {
    const auto root = makeTempRoot("crosspkg-fail");
    // Package A: singer imports "vocoder" from nonexistent package
    createPackage(root, "pkg-a", "com.test.A", "1.0.0", {"pitch"},
                  "[{\"id\": \"pitch\"}, "
                  "{\"id\": \"vocoder\", \"package\": \"com.test.Nonexistent\", \"version\": \"*\"}]");

    srt::core::Runtime runtime;
    auto result = runtime.loadPackage(root / "pkg-a");
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().type() == srt::core::Error::InvalidArgument);
    // Error message should mention the declared package
    CHECK(result.error().message().find("com.test.Nonexistent") != std::string::npos);

    fs::remove_all(root);
}

TEST_CASE("Cross-package: default import fails when not in own package",
          "[package][isolation]") {
    const auto root = makeTempRoot("crosspkg-default-fail");
    // Package A: singer imports "vocoder" WITHOUT cross-package declaration,
    // but A doesn't have a "vocoder" inference
    createPackage(root, "pkg-a", "com.test.A", "1.0.0", {"pitch"},
                  "[{\"id\": \"pitch\"}, {\"id\": \"vocoder\"}]");
    // Package B: has "vocoder" but A didn't declare cross-package import
    createPackage(root, "pkg-b", "com.test.B", "1.0.0", {"vocoder"});

    srt::core::Runtime runtime;
    REQUIRE(runtime.loadPackage(root / "pkg-b").hasValue());
    // A's singer should fail: "vocoder" not found in package A (strict isolation)
    auto result = runtime.loadPackage(root / "pkg-a");
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().message().find("vocoder") != std::string::npos);
    CHECK(result.error().message().find("com.test.A") != std::string::npos);

    fs::remove_all(root);
}
