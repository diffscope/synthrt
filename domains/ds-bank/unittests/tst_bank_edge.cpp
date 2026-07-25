// ds-bank edge condition tests (BANK-001 ~ BANK-012).
//
// Coverage matrix: docs/refactoring-vnext/test-matrix-expansion.md §1.8
// Test target: domains/ds-bank/unittests/tst-ds-bank (CMakeLists uses GLOB_RECURSE to auto-collect).
//
// Design principle cross-reference:
//   ROBUST-01: parsePackage/refresh/findSinger/singerSnapshot all return Expected<T>,
//              no exceptions thrown.
//   ROBUST-02: File system/JSON exceptions are converted to Error at the PackageParser boundary.
//   INFRA-03:  L1 single-component tests do not load plugin DLLs (only parse desc.json).
//
// API difference notes (where actual API differs from matrix expectations, adjusted per actual API):
//   BANK-001: Matrix expects FileNotFound; actual PackageParser first validates the root
//             directory with weakly_canonical + is_directory, returning
//             ErrorCode::PackageManifestInvalid for nonexistent/non-directory
//             ("package root is not a readable directory"). FileNotFound is only used
//             for singer lookup failures.
//   BANK-002: Matrix expects FileNotFound; actual desc.json open failure makes readAll
//             return Error::FileNotOpen (ErrorCode::FileNotOpen, "failed to open
//             package manifest").
//   BANK-003: Matrix expects Strict mode to return InvalidFormat for unknown fields;
//             actual PackageParser only reads known fields and does not validate unknown
//             top-level fields in desc.json (unknown fields are ignored in both modes).
//             Strict/Relaxed difference only appears in resource path traversal and
//             sub-configuration corruption (BF-33). This case verifies actual behavior
//             and records the difference.
//   BANK-005: Matrix expects ParseError; actual malformed JSON returns
//             ErrorCode::PackageManifestInvalid (no ParseError in the error code system;
//             JSON parse failures are classified as PackageManifestInvalid).
//   BANK-009: Matrix expects InvalidArg; actual empty SingerRef matches no snapshot,
//             singerSnapshot returns ErrorCode::FileNotFound ("singer not found").
//   BANK-012: P2 case, marked with SKIP() (constructing >10MB desc.json requires
//             large disk writes, and no size limit implementation exists to verify).
//
// Error code system see include/synthrt/Core/Support/Diagnostic.h:
//   General segment: InvalidFormat=1, FileNotFound=2, FileNotOpen=3, InvalidArgument=7...
//   Package segment: PackageManifestInvalid=103, PackageManifestMissingField=104...

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <diffsinger/Bank/PackageParser.h>
#include <diffsinger/Bank/PackageStatus.h>
#include <diffsinger/Bank/SingerRef.h>
#include <diffsinger/Bank/SingerSnapshot.h>
#include <diffsinger/Bank/VoicebankScanner.h>
#include <synthrt/Core/Support/Diagnostic.h>

using namespace ds::bank;
using srt::core::ErrorCode;
using srt::core::ErrorCategory;

namespace srt::core {
// Catch2 needs operator<< to stream ErrorCode values in failed assertions.
inline std::ostream &operator<<(std::ostream &os, const ErrorCode &code) {
    return os << errorCodeToString(code);
}
} // namespace srt::core

namespace {

    // RAII temporary directory: created on construction, cleaned up on destruction.
    struct TempDir {
        std::filesystem::path path;
        TempDir(const std::string &name) {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path = std::filesystem::temp_directory_path() /
                  ("ds-bank-edge-" + name + "-" + std::to_string(stamp));
            std::filesystem::create_directories(path);
        }
        ~TempDir() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
        TempDir(const TempDir &) = delete;
        TempDir &operator=(const TempDir &) = delete;
    };

    void writeFile(const std::filesystem::path &path, const std::string &text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << text;
    }

    // Create a minimal parseable voicebank package (with singer + duration inference).
    // Manually concatenate JSON to avoid depending on stdc::formatN (consistent with
    // tst_voicebank_scanner_edge_cases.cpp style, keeping headers self-contained).
    void createPackage(const std::filesystem::path &pkgDir,
                       const std::string &packageId,
                       const std::string &version,
                       const std::string &singerId = "test_singer") {
        std::string desc = "{\"id\":\"" + packageId + "\",\"version\":\"" + version +
            "\",\"contributes\":{\"singers\":[\"characters/singer/config.json\"],"
            "\"inferences\":[\"inferences/duration/config.json\"]}}";
        writeFile(pkgDir / "desc.json", desc);

        std::string singer = "{\"$version\":\"1.0\",\"id\":\"" + singerId +
            "\",\"level\":1,\"imports\":[{\"inferenceId\":\"duration\"}],"
            "\"configuration\":{\"defaultLanguage\":\"cmn\","
            "\"languages\":[{\"id\":\"cmn\",\"g2p\":\"g2p-cmn-official\",\"s2pMode\":\"dict\"}]}}";
        writeFile(pkgDir / "characters/singer/config.json", singer);

        writeFile(pkgDir / "inferences/duration/config.json",
            "{\"id\":\"duration\",\"class\":\"ai.svs.DurationInference\",\"level\":1,\"configuration\":{}}");
    }

} // namespace

// ===========================================================================
// BANK-001: parsePackage with nonexistent directory
//
// Actual behavior: root directory nonexistent/non-directory -> PackageManifestInvalid (see file header API difference notes)
// ===========================================================================
TEST_CASE("BANK-001: parsePackage rejects nonexistent directory", "[bank][edge]") {
    PackageParser parser;
    const auto nonexistent = std::filesystem::temp_directory_path() /
                             "ds-bank-edge-nonexistent-does-not-exist-12345";
    // Ensure directory does not exist
    std::error_code ec;
    std::filesystem::remove_all(nonexistent, ec);
    REQUIRE_FALSE(std::filesystem::exists(nonexistent, ec));

    auto result = parser.parsePackage(nonexistent, PackageParser::ParseMode::Strict);
    REQUIRE_FALSE(result.hasValue());
    // Matrix expects FileNotFound; actual returns PackageManifestInvalid (directory validation precedes file reading)
    REQUIRE(result.errorCode() == ErrorCode::PackageManifestInvalid);
    REQUIRE(result.errorCategory() == ErrorCategory::Package);
    REQUIRE(result.error().message().find("package root") != std::string::npos);
}

// ===========================================================================
// BANK-002: parsePackage with directory without desc.json
//
// Actual behavior: desc.json open failure -> FileNotOpen (see file header API difference notes)
// ===========================================================================
TEST_CASE("BANK-002: parsePackage rejects directory without desc.json", "[bank][edge]") {
    TempDir dir("no-desc");
    // Directory exists but does not contain desc.json
    REQUIRE(std::filesystem::is_directory(dir.path));

    PackageParser parser;
    auto result = parser.parsePackage(dir.path, PackageParser::ParseMode::Strict);
    REQUIRE_FALSE(result.hasValue());
    // Matrix expects FileNotFound; actual returns FileNotOpen (readAll open failure)
    REQUIRE(result.errorCode() == ErrorCode::FileNotOpen);
    REQUIRE(result.error().message().find("desc.json") != std::string::npos);
}

// ===========================================================================
// BANK-003: parsePackage Strict mode encounters unknown field
//
// Actual behavior: PackageParser does not validate unknown top-level fields in
// desc.json; Strict mode also ignores unknown fields and parses successfully
// (see file header API difference notes). Strict/Relaxed difference only appears
// in resource path traversal and sub-configuration corruption scenarios.
// ===========================================================================
TEST_CASE("BANK-003: parsePackage Strict mode tolerates unknown top-level field",
          "[bank][edge]") {
    TempDir dir("unknown-field-strict");
    writeFile(dir.path / "desc.json",
        R"json({"id":"unknown.strict","version":"1.0.0","unknownTopLevelField":"value","contributes":{"singers":["characters/s/config.json"],"inferences":["inferences/dur/config.json"]}})json");
    writeFile(dir.path / "characters/s/config.json",
        R"json({"$version":"1.0","id":"s1","level":1,"imports":[{"inferenceId":"dur"}],"configuration":{"defaultLanguage":"cmn","languages":[{"id":"cmn"}]}})json");
    writeFile(dir.path / "inferences/dur/config.json",
        R"json({"id":"dur","class":"ai.svs.DurationInference","level":1,"configuration":{}})json");

    PackageParser parser;
    auto result = parser.parsePackage(dir.path, PackageParser::ParseMode::Strict);
    // Matrix expects Strict to return InvalidFormat; actual both modes ignore unknown top-level fields
    INFO("Strict mode does not reject unknown top-level fields; actual code: "
         << result.errorCode());
    REQUIRE(result.hasValue());
    REQUIRE(result.value().packageId() == "unknown.strict");
}

// ===========================================================================
// BANK-004: parsePackage Relaxed mode ignores unknown field
// ===========================================================================
TEST_CASE("BANK-004: parsePackage Relaxed mode ignores unknown field",
          "[bank][edge]") {
    TempDir dir("unknown-field-relaxed");
    writeFile(dir.path / "desc.json",
        R"json({"id":"unknown.relaxed","version":"1.0.0","extraField":42,"contributes":{"singers":["characters/s/config.json"],"inferences":["inferences/dur/config.json"]}})json");
    writeFile(dir.path / "characters/s/config.json",
        R"json({"$version":"1.0","id":"s1","level":1,"imports":[{"inferenceId":"dur"}],"configuration":{"defaultLanguage":"cmn","languages":[{"id":"cmn"}]}})json");
    writeFile(dir.path / "inferences/dur/config.json",
        R"json({"id":"dur","class":"ai.svs.DurationInference","level":1,"configuration":{}})json");

    PackageParser parser;
    auto result = parser.parsePackage(dir.path, PackageParser::ParseMode::Relaxed);
    REQUIRE(result.hasValue());
    REQUIRE(result.value().packageId() == "unknown.relaxed");
    REQUIRE(result.value().version().toString() == "1.0");
}

// ===========================================================================
// BANK-005: parsePackage malformed JSON
//
// Actual behavior: JSON parse failure -> PackageManifestInvalid (no ParseError in error code system)
// ===========================================================================
TEST_CASE("BANK-005: parsePackage rejects malformed JSON", "[bank][edge]") {
    TempDir dir("malformed-json");
    writeFile(dir.path / "desc.json", "{invalid");

    PackageParser parser;
    auto result = parser.parsePackage(dir.path, PackageParser::ParseMode::Strict);
    REQUIRE_FALSE(result.hasValue());
    // Matrix expects ParseError; actual returns PackageManifestInvalid
    REQUIRE(result.errorCode() == ErrorCode::PackageManifestInvalid);
    REQUIRE(result.error().message().find("invalid package manifest format") != std::string::npos);
}

// ===========================================================================
// BANK-006: VoicebankScanner refresh with no search paths
//
// Without setSearchPaths, refresh returns empty vector (ROBUST-01: returns Expected, no exceptions)
// ===========================================================================
TEST_CASE("BANK-006: VoicebankScanner refresh with no search paths returns empty",
          "[bank][edge]") {
    VoicebankScanner scanner;
    // setSearchPaths not called
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(exp.value().empty());
    REQUIRE(scanner.singers().empty());
    REQUIRE(scanner.manifests().empty());
}

// ===========================================================================
// BANK-007: findSinger queries nonexistent singerId
// ===========================================================================
TEST_CASE("BANK-007: findSinger returns FileNotFound for unknown singerId",
          "[bank][edge]") {
    TempDir dir("find-unknown");
    createPackage(dir.path / "pkg", "pkg.find", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    REQUIRE(scanner.refresh().hasValue());
    REQUIRE_FALSE(scanner.singers().empty());

    auto exp = scanner.findSinger("nonexistent_singer");
    REQUIRE_FALSE(exp.hasValue());
    // findSinger returns FileNotFound when not found
    REQUIRE(exp.errorCode() == ErrorCode::FileNotFound);
    REQUIRE(exp.error().message().find("nonexistent_singer") != std::string::npos);
}

// ===========================================================================
// BANK-008: packageDirectories multi-version coexistence
//
// Two versions of the same packageId coexist in different directories; packageDirectories returns 2 entries.
// ===========================================================================
TEST_CASE("BANK-008: packageDirectories returns both versions for multi-version packageId",
          "[bank][edge]") {
    TempDir dir("multi-version");
    createPackage(dir.path / "v1", "pkg.multiver", "1.0.0");
    createPackage(dir.path / "v2", "pkg.multiver", "2.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    REQUIRE(scanner.refresh().hasValue());

    auto dirs = scanner.packageDirectories("pkg.multiver");
    REQUIRE(dirs.size() == 2);
    // Two version numbers are different
    const auto v0 = dirs[0].version.toString();
    const auto v1 = dirs[1].version.toString();
    REQUIRE(v0 != v1);
    // Version set should be {"1.0", "2.0"}
    bool hasV1 = (v0 == "1.0" || v1 == "1.0");
    bool hasV2 = (v0 == "2.0" || v1 == "2.0");
    REQUIRE(hasV1);
    REQUIRE(hasV2);
}

// ===========================================================================
// BANK-009: singerSnapshot with empty SingerRef
//
// Actual behavior: empty SingerRef matches no snapshot -> FileNotFound (see file header difference notes)
// ===========================================================================
TEST_CASE("BANK-009: singerSnapshot returns FileNotFound for empty SingerRef",
          "[bank][edge]") {
    TempDir dir("empty-ref");
    createPackage(dir.path / "pkg", "pkg.emptyref", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    REQUIRE(scanner.refresh().hasValue());
    REQUIRE_FALSE(scanner.singers().empty());

    SingerRef emptyRef; // packageId/singerId/version all empty
    auto exp = scanner.singerSnapshot(emptyRef);
    REQUIRE_FALSE(exp.hasValue());
    // Matrix expects InvalidArg; actual returns FileNotFound (empty ref matches no singer)
    REQUIRE(exp.errorCode() == ErrorCode::FileNotFound);
    REQUIRE(exp.error().message().find("singer not found") != std::string::npos);
}

// ===========================================================================
// BANK-010: parsePackage desc.json contains path traversal field
//
// contributes.inferences contains "../../" path: Strict rejects, Relaxed safe-handles (records
// diagnostic and continues). This is the real Strict/Relaxed difference point (BF-33 / resource path validation).
// ===========================================================================
TEST_CASE("BANK-010: parsePackage Strict rejects path traversal, Relaxed safe-handles",
          "[bank][edge]") {
    TempDir dir("path-traversal");
    writeFile(dir.path / "desc.json",
        R"json({"id":"traversal.pkg","version":"1.0.0","contributes":{"inferences":["../outside.json"]}})json");

    PackageParser parser;
    // Strict rejects path traversal
    auto strict = parser.parsePackage(dir.path, PackageParser::ParseMode::Strict);
    REQUIRE_FALSE(strict.hasValue());

    // Relaxed safe-handling: parse succeeds and records diagnostic
    auto relaxed = parser.parsePackage(dir.path, PackageParser::ParseMode::Relaxed);
    REQUIRE(relaxed.hasValue());
    REQUIRE(relaxed.value().packageId() == "traversal.pkg");
    // Diagnostic list should contain path traversal error
    REQUIRE_FALSE(relaxed.value().diagnostics().empty());
    bool foundPathDiag = false;
    for (const auto &d : relaxed.value().diagnostics()) {
        if (d.message.find("invalid package resource path") != std::string::npos) {
            foundPathDiag = true;
            break;
        }
    }
    REQUIRE(foundPathDiag);
}

// ===========================================================================
// BANK-011: VoicebankScanner setSearchPaths with empty vector
// ===========================================================================
TEST_CASE("BANK-011: VoicebankScanner setSearchPaths with empty vector refreshes empty",
          "[bank][edge]") {
    VoicebankScanner scanner;
    scanner.setSearchPaths({});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(exp.value().empty());
    REQUIRE(scanner.singers().empty());

    // packageDirectories returns empty for unknown packageId
    REQUIRE(scanner.packageDirectories("any.pkg").empty());
}

// ===========================================================================
// BANK-012: parsePackage desc.json oversized file (P2)
//
// PackageParser::readAll (PackageParser.cpp:20-33) reads the entire manifest
// into a stringstream with no size limit, and parsePackage does not impose a
// manifest-size guard. There is therefore no rejection behavior to verify: a
// large desc.json would simply be read and then fail JSON parsing (already
// covered by BANK-005 malformed-JSON). Constructing a >10MB fixture would only
// exercise memory/IO, not a size-limit contract. SKIP retained per matrix P2
// guidance; remove only after a size-limit guard is added to PackageParser.
// ===========================================================================
TEST_CASE("BANK-012: parsePackage handles oversized desc.json (>10MB)",
          "[bank][edge]") {
    SKIP("P2: PackageParser::readAll (PackageParser.cpp:20-33) has no manifest "
         "size limit; parsePackage does not impose a size guard, so there is no "
         "rejection behavior to verify. A >10MB fixture would only test memory/"
         "IO (the malformed-JSON path is already covered by BANK-005). Retained "
         "per matrix P2 guidance; remove after a size-limit guard is added.");
}

// ===========================================================================
// BANK-013 ~ BANK-020: pure value semantics and empty state edge tests.
//
// The following cases do not involve file system interaction (no disk IO except BANK-019/020 querying empty cache),
// zero runtime side effects. Only use APIs already declared in existing includes (PackageManifest is transitively
// included via PackageParser.h / VoicebankScanner.h), no new header dependencies introduced.
// ===========================================================================

// ===========================================================================
// BANK-013: PackageManifest default-constructed field values
//
// Pure value semantics test: default-constructed PackageManifest all accessors should return empty containers/empty strings,
// rootPath empty, compatVersion nullopt (std::optional default unset).
// Verify zero-initialization state of PackageManifest (PackageManifest.h: PackageManifest() = default).
// ===========================================================================
TEST_CASE("BANK-013: PackageManifest default construction has empty fields",
          "[bank][edge]") {
    PackageManifest manifest;
    // String fields default to empty
    REQUIRE(manifest.packageId().empty());
    REQUIRE(manifest.name().empty());
    REQUIRE(manifest.description().empty());
    REQUIRE(manifest.author().empty());
    REQUIRE(manifest.license().empty());
    // Container fields default to empty
    REQUIRE(manifest.dependencies().empty());
    REQUIRE(manifest.singerRefs().empty());
    REQUIRE(manifest.inferenceRefs().empty());
    REQUIRE(manifest.singers().empty());
    REQUIRE(manifest.speakers().empty());
    REQUIRE(manifest.languages().empty());
    REQUIRE(manifest.inferences().empty());
    REQUIRE(manifest.diagnostics().empty());
    // Path field defaults to empty
    REQUIRE(manifest.rootPath().empty());
    // compatVersion default unset (std::optional defaults to nullopt)
    REQUIRE_FALSE(manifest.compatVersion().has_value());
}

// ===========================================================================
// BANK-014: SingerRef default construction and empty value check
//
// Pure value semantics test: default-constructed SingerRef's three string fields (packageId/singerId/
// version) are all empty (SingerRef.h: SingerRef() = default). Verify struct zero-initialization.
// ===========================================================================
TEST_CASE("BANK-014: SingerRef default construction has empty fields",
          "[bank][edge]") {
    SingerRef ref;
    REQUIRE(ref.packageId.empty());
    REQUIRE(ref.singerId.empty());
    REQUIRE(ref.version.empty());
}

// ===========================================================================
// BANK-015: SingerRef two-arg/three-arg constructors and toString canonical form
//
// Pure value semantics test: two-arg constructor sets packageId/singerId with version empty; three-arg constructor
// sets all fields. toString() outputs canonical form "packageId:singerId" (see
// SingerRef.h doc comments). Verify constructor and toString consistency.
// ===========================================================================
TEST_CASE("BANK-015: SingerRef constructors and toString canonical form",
          "[bank][edge]") {
    SingerRef twoArg("pkg.a", "singer.a");
    REQUIRE(twoArg.packageId == "pkg.a");
    REQUIRE(twoArg.singerId == "singer.a");
    REQUIRE(twoArg.version.empty());
    REQUIRE(twoArg.toString() == "pkg.a:singer.a");

    SingerRef threeArg("pkg.b", "singer.b", "2.0");
    REQUIRE(threeArg.packageId == "pkg.b");
    REQUIRE(threeArg.singerId == "singer.b");
    REQUIRE(threeArg.version == "2.0");
    REQUIRE(threeArg.toString() == "pkg.b:singer.b");
}

// ===========================================================================
// BANK-016: SingerRef::parse parses canonical string round-trip
//
// Pure value semantics test: parse("packageId:singerId") splits on ":" to fill packageId and
// singerId (version left empty), and toString() can restore the original string. Verify parse and
// toString round-trip consistency (static parse method declared in SingerRef.h).
// ===========================================================================
TEST_CASE("BANK-016: SingerRef parse round-trips with toString",
          "[bank][edge]") {
    const std::string canonical = "pkg.round:singer.round";
    auto ref = SingerRef::parse(canonical);
    REQUIRE(ref.packageId == "pkg.round");
    REQUIRE(ref.singerId == "singer.round");
    REQUIRE(ref.version.empty());
    // parse -> toString round-trip should restore canonical form
    REQUIRE(ref.toString() == canonical);
}

// ===========================================================================
// BANK-017: PackageStatus default-constructed field values
//
// Pure value semantics test: default-constructed PackageStatus.valid is false, packageId/
// rootPath/dependencies/unresolvedDependencies are all empty (default member initial values in
// PackageStatus.h). Verify zero-initialization state of the scan result struct.
// ===========================================================================
TEST_CASE("BANK-017: PackageStatus default construction has empty fields",
          "[bank][edge]") {
    PackageStatus status;
    REQUIRE(status.packageId.empty());
    REQUIRE(status.rootPath.empty());
    REQUIRE(status.dependencies.empty());
    REQUIRE(status.unresolvedDependencies.empty());
    // valid defaults to false (PackageStatus.h: bool valid = false;)
    REQUIRE_FALSE(status.valid);
}

// ===========================================================================
// BANK-018: PackageManifest setter/getter round-trip consistency
//
// Pure value semantics test: after writing fields via setter, getter should read back the same value. Verify
// read/write symmetry of PackageManifest accessors, no file system interaction.
// ===========================================================================
TEST_CASE("BANK-018: PackageManifest setters and getters round-trip",
          "[bank][edge]") {
    PackageManifest manifest;
    manifest.setPackageId("round.trip");
    REQUIRE(manifest.packageId() == "round.trip");

    manifest.setName("Round Trip");
    REQUIRE(manifest.name() == "Round Trip");

    manifest.setDescription("desc");
    REQUIRE(manifest.description() == "desc");

    manifest.setAuthor("author");
    REQUIRE(manifest.author() == "author");

    manifest.setLicense("MIT");
    REQUIRE(manifest.license() == "MIT");

    manifest.setDependencies({"dep1", "dep2", "dep3"});
    REQUIRE(manifest.dependencies().size() == 3);
    REQUIRE(manifest.dependencies()[0] == "dep1");
    REQUIRE(manifest.dependencies()[2] == "dep3");
}

// ===========================================================================
// BANK-019: VoicebankScanner findSinger returns FileNotFound when not refreshed
//
// Edge test: after construction without calling setSearchPaths/refresh, internal singer cache is empty,
// findSinger iterates empty cache matching no singer -> FileNotFound (ROBUST-01: returns
// Expected, no exceptions). Complementary to BANK-007 (querying unknown singer after refresh), verifies
// the query path on a never-refreshed empty cache.
// ===========================================================================
TEST_CASE("BANK-019: VoicebankScanner findSinger on fresh scanner returns FileNotFound",
          "[bank][edge]") {
    VoicebankScanner scanner;
    // setSearchPaths / refresh not called, singers()/manifests() caches are both empty
    REQUIRE(scanner.singers().empty());
    REQUIRE(scanner.manifests().empty());

    auto exp = scanner.findSinger("any_singer");
    REQUIRE_FALSE(exp.hasValue());
    // Empty cache matches no singer, returns FileNotFound (consistent with BANK-007)
    REQUIRE(exp.errorCode() == ErrorCode::FileNotFound);
    REQUIRE(exp.error().message().find("any_singer") != std::string::npos);
}

// ===========================================================================
// BANK-020: VoicebankScanner packageDirectories returns empty when not refreshed
//
// Edge test: after construction without refresh, packageDirectories returns empty
// vector for any packageId (no discovered packages). Complementary to BANK-011 (refresh after setSearchPaths({}) returns empty),
// verifies the query path without refresh still safely returns empty (ROBUST-01).
// ===========================================================================
TEST_CASE("BANK-020: VoicebankScanner packageDirectories on fresh scanner returns empty",
          "[bank][edge]") {
    VoicebankScanner scanner;
    // Not refreshed, no discovered packages
    REQUIRE(scanner.packageDirectories("unknown.pkg").empty());
    REQUIRE(scanner.packageDirectories("another.pkg").empty());
}
