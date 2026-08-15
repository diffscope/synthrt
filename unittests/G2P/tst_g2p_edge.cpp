// G2P edge-case tests (G2P-001 ~ G2P-012).
//
// Covers edge inputs and error paths of the PackageManager / Manager public APIs. Written
// based on actual API signatures and implementation behavior; deviations from the test
// matrix description are noted at the file top and in each case's comments.
//
// API difference notes (relative to test matrix):
//   - G2P-002: addPackagePath with nonexistent path actually returns
//     ErrorCode::G2pFileSystemError (matrix describes FileNotFound). The G2P error code
//     range has no standalone FileNotFound; filesystem-class errors are uniformly mapped
//     to G2pFileSystemError (see PackageManager.cpp).
//   - G2P-006: open with malformed package.json actually returns ErrorCode::G2pConfigError
//     (matrix describes ParseError). JSON parse failures are reported as ConfigError by
//     PackageData::readDesc (see Package.cpp).
//   - G2P-011: task with nonexistent category actually returns ErrorCode::G2pRouteNotFound
//     (matrix describes NotFound).
//   - G2P-001: empty context name + non-empty version returns G2pValidationError (R-8:
//     default context cannot carry a version); empty context name + empty version registers
//     as the default context (success) on a valid path, corresponding to the matrix entry
//     "Error(InvalidArg) or default context".
//
// Singleton note: Manager is a process-level singleton (Manager::instance()). G2P-003/004/
// 009/010/011 use the singleton; G2P-001/002/005/006/007/008 use a local PackageManager
// instance to avoid polluting singleton state. G2P-003/004 inject a minimal default-context
// package (level 2, no dependencies, one g2p module) via ensureManagerInitialized() so that
// initialize() can succeed (when the task plugin is unregistered, createModuleTask fails
// with only a warning and does not block initialize).
//
// G2P-012 (P2) requires destructively triggering a default-context initialization failure,
// which cannot be reliably reproduced in a unit test (a default-context failure makes
// initialize() return G2pInitializationError early without writing m_contextStates); SKIP
// per P2 strategy.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/Core/Manager.h>
#include <synthrt/G2P/Core/PackageManager.h>
#include <synthrt/G2P/Support/PhonemeDict.h>

using namespace srt::g2p;
using srt::core::ContextKey;
using srt::core::ErrorCode;

namespace srt::core {
// Catch2 needs operator<< to stream ErrorCode values in failed assertions.
inline std::ostream &operator<<(std::ostream &os, const ErrorCode &code) {
    return os << errorCodeToString(code);
}
} // namespace srt::core

namespace {

    std::filesystem::path makeTempDir(const std::string &name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = std::filesystem::temp_directory_path() /
                   ("srt-g2p-edge-" + name + "-" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        return dir;
    }

    void writeFile(const std::filesystem::path &path, const std::string &text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << text;
    }

    /// Ensure the Manager singleton reaches the initialized state so that
    /// idempotency / post-initialize contracts can be exercised. Adds a minimal
    /// default-context package (level 2, one g2p module, no dependencies) so
    /// Manager::initialize() can succeed even when no official G2P packages are
    /// present. Idempotent: safe to call from multiple TEST_CASEs.
    void ensureManagerInitialized(Manager *mgr) {
        static bool packageAdded = false;
        if (!packageAdded) {
            const auto root = makeTempDir("default-pkg");
            const auto pkgDir = root / "edge_default_pkg";
            std::filesystem::create_directories(pkgDir);
            writeFile(pkgDir / "package.json",
                      R"({"packageId":"edge.default","version":"1.0.0","level":2,)"
                      R"("modules":{"g2p":[{"moduleId":"edge-g2p","class":"srt.g2p.task"}]}})");
            auto addExp = mgr->addPackagePath("", stdc::VersionNumber{}, root);
            (void)addExp;
            packageAdded = true;
        }
        auto initExp = mgr->initialize();
        (void)initExp;
    }

} // namespace

// ===========================================================================
// G2P-001/002: addPackagePath error cases
//   Merged: both verify addPackagePath rejection contracts and share the
//   PackageManager construction. SECTION form preserves per-case
//   traceability while removing the duplicate setup. Adds an empty-path
//   boundary case.
// ===========================================================================
TEST_CASE("G2P-001/002: addPackagePath error cases", "[g2p][edge]") {
    PackageManager pm;
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();

    SECTION("G2P-001a: empty context + non-empty version -> G2pValidationError") {
        // R-8: default context cannot carry a version.
        auto exp = pm.addPackagePath("", v1, std::filesystem::temp_directory_path());
        REQUIRE(!exp.hasValue());
        REQUIRE(exp.takeError().code() == ErrorCode::G2pValidationError);
    }

    SECTION("G2P-001b: empty context + empty version + valid path -> default context") {
        const auto root = makeTempDir("g2p001-default");
        auto exp = pm.addPackagePath("", stdc::VersionNumber{}, root);
        REQUIRE(exp.hasValue());

        const auto keys = pm.contextKeys();
        REQUIRE(keys.size() == 1);
        REQUIRE(keys[0].isDefault());

        std::filesystem::remove_all(root);
    }

    SECTION("G2P-002: nonexistent path -> G2pFileSystemError") {
        // API difference: matrix expected FileNotFound; actual returns
        // G2pFileSystemError (no standalone FileNotFound in the G2P range).
        auto exp = pm.addPackagePath("ctx", v1, "/nonexistent/path/does/not/exist");
        REQUIRE(!exp.hasValue());
        REQUIRE(exp.takeError().code() == ErrorCode::G2pFileSystemError);
    }

    SECTION("G2P-002b: empty path -> G2pFileSystemError") {
        // Boundary: empty path must also be rejected with G2pFileSystemError.
        auto exp = pm.addPackagePath("ctx", v1, std::filesystem::path{});
        REQUIRE(!exp.hasValue());
        REQUIRE(exp.takeError().code() == ErrorCode::G2pFileSystemError);
    }
}

// ===========================================================================
// G2P-003: Manager::initialize is hard-idempotent (D11)
//   Calling again after a successful initialize returns G2pAlreadyInitialized.
// ===========================================================================
TEST_CASE("G2P-003: Manager::initialize is hard idempotent", "[g2p][edge]") {
    Manager *mgr = Manager::instance();
    ensureManagerInitialized(mgr);
    REQUIRE(mgr->initialized());

    auto second = mgr->initialize();
    REQUIRE(!second.hasValue());
    REQUIRE(second.takeError().code() == ErrorCode::G2pAlreadyInitialized);
    REQUIRE(mgr->initialized());
}

// ===========================================================================
// G2P-004: removeContextsByPrefix called after initialize
//   Calling after initialize returns G2pAlreadyInitialized.
// ===========================================================================
TEST_CASE("G2P-004: removeContextsByPrefix after initialize fails", "[g2p][edge]") {
    Manager *mgr = Manager::instance();
    ensureManagerInitialized(mgr);
    REQUIRE(mgr->initialized());

    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();
    auto exp = mgr->removeContextsByPrefix("edge_prefix", v1);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.takeError().code() == ErrorCode::G2pAlreadyInitialized);
}

// ===========================================================================
// G2P-005: contextState querying an unregistered context
//   Returns ContextState::NotRegistered.
// ===========================================================================
TEST_CASE("G2P-005: contextState for unregistered context is NotRegistered",
          "[g2p][edge]") {
    PackageManager pm;
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();
    const ContextKey unregistered("unregistered_ctx", v1);
    REQUIRE(pm.contextState(unregistered) == ContextState::NotRegistered);
}

// ===========================================================================
// G2P-006: open with malformed package.json
//   Actually returns G2pConfigError (matrix ParseError; see file header note).
// ===========================================================================
TEST_CASE("G2P-006: open with malformed package.json", "[g2p][edge]") {
    PackageManager pm;
    const auto root = makeTempDir("g2p006-malformed");
    writeFile(root / "package.json", "{ this is : not valid json ]");

    auto exp = pm.open(root);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.takeError().code() == ErrorCode::G2pConfigError);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// G2P-007: find querying nonexistent packageId
//   Returns empty Package (isValid() == false).
// ===========================================================================
TEST_CASE("G2P-007: find with nonexistent packageId returns empty Package",
          "[g2p][edge]") {
    PackageManager pm;
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();

    auto pkg = pm.find("nonexistent_pkg_id", v1);
    REQUIRE(!pkg.isValid());
}

// ===========================================================================
// G2P-008: multi-version coexistence under same packageId
//   After both versions are loaded, find returns the corresponding version respectively.
// ===========================================================================
TEST_CASE("G2P-008: multi-version same packageId coexistence", "[g2p][edge]") {
    PackageManager pm;
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();
    const auto v2 = stdc::VersionNumber::fromString("2.0.0").value();
    const std::string pkgId = "edge.coxist";

    const auto dir1 = makeTempDir("g2p008-v1");
    const auto dir2 = makeTempDir("g2p008-v2");
    writeFile(dir1 / "package.json",
              R"({"packageId":")" + pkgId + R"(","version":"1.0.0"})");
    writeFile(dir2 / "package.json",
              R"({"packageId":")" + pkgId + R"(","version":"2.0.0"})");

    auto open1 = pm.open(dir1);
    REQUIRE(open1.hasValue());
    auto open2 = pm.open(dir2);
    REQUIRE(open2.hasValue());

    auto found1 = pm.find(pkgId, v1);
    REQUIRE(found1.isValid());
    REQUIRE(found1.version() == v1);

    auto found2 = pm.find(pkgId, v2);
    REQUIRE(found2.isValid());
    REQUIRE(found2.version() == v2);

    std::filesystem::remove_all(dir1);
    std::filesystem::remove_all(dir2);
}

// ===========================================================================
// G2P-009: convert with empty input vector
//   After initialize, convert({}) returns an empty vector.
// ===========================================================================
TEST_CASE("G2P-009: convert with empty input vector returns empty",
          "[g2p][edge]") {
    Manager *mgr = Manager::instance();
    auto results = mgr->convert({});
    REQUIRE(results.empty());
}

// ===========================================================================
// G2P-010: convert with G2pInput containing empty string
//   Returns empty/error results, does not crash.
// ===========================================================================
TEST_CASE("G2P-010: convert with empty-string G2pInput does not crash",
          "[g2p][edge]") {
    Manager *mgr = Manager::instance();
    auto results = mgr->convert({G2pInput{"", ""}});
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].lyric.empty());
}

// ===========================================================================
// G2P-011: task querying nonexistent category
//   Actually returns G2pRouteNotFound (matrix NotFound; see file header note).
// ===========================================================================
TEST_CASE("G2P-011: task with nonexistent category returns RouteNotFound",
          "[g2p][edge]") {
    Manager *mgr = Manager::instance();
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();

    auto exp = mgr->task("nonexistent_category", "ctx", v1, "some_id");
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.takeError().code() == ErrorCode::G2pRouteNotFound);
}

// ===========================================================================
// G2P-012: failedContexts excludes default context (P2)
//   After default-context initialization failure, failedContexts() does not include the default context.
//
//   Cannot be reliably reproduced in a unit test: default-context initialization failure
//   makes Manager::initialize() return G2pInitializationError early and does not write the
//   default context into m_contextStates, so the runtime scenario "default context in
//   Failed state" cannot be constructed. Implementation invariant
//   (failedContexts() filters ctxKey.isDefault()) see PackageManager.cpp:
//     if (state == ContextState::Failed && !ctxKey.isDefault())
//   SKIP per P2 strategy.
// ===========================================================================
TEST_CASE("G2P-012: failedContexts excludes default context", "[g2p][edge]") {
    SKIP("P2: triggering a default-context initialization failure aborts "
         "Manager::initialize() (G2pInitializationError) before the default "
         "context is recorded in m_contextStates, so the Failed-state default "
         "context cannot be reproduced in a unit test. Implementation invariant "
         "(failedContexts filters ctxKey.isDefault()) verified by code "
         "inspection of PackageManager::failedContexts().");
}

// ===========================================================================
// G2P-013: ContextKey default construction and default-context detection
//   A default-constructed ContextKey should represent the default context: context is empty,
//   version is empty, isDefault() is true, isVersioned() is false, toString() == "(default)".
//   Covers the contract of ContextKey's default state, preventing later refactors from
//   breaking default-context semantics.
// ===========================================================================
TEST_CASE("G2P-013: ContextKey default construction yields default context",
          "[g2p][edge]") {
    const ContextKey key;
    REQUIRE(key.context.empty());
    REQUIRE(key.version.isEmpty());
    REQUIRE(key.isDefault());
    REQUIRE(!key.isVersioned());
    REQUIRE(key.toString() == "(default)");
}

// ===========================================================================
// G2P-014: PackageManager contextKeys returns empty on empty state
//   A freshly constructed PackageManager has no registered contexts; contextKeys() should
//   return an empty vector. Covers the initial-state contract, preventing the implementation
//   from mistakenly pre-inserting a default entry into the map.
// ===========================================================================
TEST_CASE("G2P-014: PackageManager contextKeys empty on fresh instance",
          "[g2p][edge]") {
    PackageManager pm;
    REQUIRE(pm.contextKeys().empty());
}

// ===========================================================================
// G2P-015: PackageManager packagePaths querying an unregistered context
//   Querying a (context, version) never registered via addPackagePath should return an
//   empty vector rather than an error (packagePaths is a const query interface; unregistered
//   is treated as an empty result). Covers the contract for querying unregistered contexts
//   (see PackageManager.cpp: m_contextPackagePaths find miss returns {}).
// ===========================================================================
TEST_CASE("G2P-015: PackageManager packagePaths for unregistered context is empty",
          "[g2p][edge]") {
    PackageManager pm;
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();
    REQUIRE(pm.packagePaths("unregistered_ctx", v1).empty());
}

// ===========================================================================
// G2P-016: Manager::instance() singleton is non-null and stable
//   Manager::instance() should return a non-null pointer, and multiple calls return the
//   same pointer (process-level singleton; see Manager.h: "Singleton via instance()").
//   Covers the singleton non-null and stability contract.
// ===========================================================================
TEST_CASE("G2P-016: Manager::instance returns stable non-null singleton",
          "[g2p][edge]") {
    Manager *a = Manager::instance();
    REQUIRE(a != nullptr);
    Manager *b = Manager::instance();
    REQUIRE(a == b);
}

// ===========================================================================
// G2P-017: PackageManager failedContexts returns empty on empty state
//   A freshly constructed PackageManager has no registered contexts; failedContexts()
//   should return an empty vector (no context can fail). Covers the initial-state contract.
// ===========================================================================
TEST_CASE("G2P-017: PackageManager failedContexts empty on fresh instance",
          "[g2p][edge]") {
    PackageManager pm;
    REQUIRE(pm.failedContexts().empty());
}

// ===========================================================================
// G2P-018: G2pInput default-constructed field values
//   A default-constructed G2pInput should have all string fields empty and
//   g2pContextVersion as the empty version (see LangCommon.h: G2pInput default construction
//   = default, members default-initialized). Covers the G2pInput default-state contract,
//   ensuring fallback paths like convert({}) do not depend on implicit values.
// ===========================================================================
TEST_CASE("G2P-018: G2pInput default constructed fields are empty",
          "[g2p][edge]") {
    const G2pInput in;
    REQUIRE(in.lyric.empty());
    REQUIRE(in.g2pId.empty());
    REQUIRE(in.g2pContext.empty());
    REQUIRE(in.g2pContextVersion.isEmpty());
}

// ===========================================================================
// G2P-019: VersionNumber empty version and comparison
//   A default-constructed VersionNumber has isEmpty() == true; a non-empty version from
//   fromString has isEmpty() == false; the empty version equals itself and does not equal
//   a non-empty version. Covers VersionNumber empty-state and equality semantics
//   (ContextKey depends on version == version and version < version; see ContextKey.h).
// ===========================================================================
TEST_CASE("G2P-019: VersionNumber empty version construction and comparison",
          "[g2p][edge]") {
    const stdc::VersionNumber empty;
    REQUIRE(empty.isEmpty());

    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();
    REQUIRE(!v1.isEmpty());
    REQUIRE(empty == stdc::VersionNumber{});
    REQUIRE(!(empty == v1));
}

// ===========================================================================
// G2P-020: multiple addPackagePath calls accumulate distinct paths for same context
//   Calling addPackagePath multiple times with different directory paths for the same
//   (context, version) should have packagePaths() return all accumulated paths
//   (see PackageManager.cpp: paths.push_back(canonical) accumulates; duplicate canonical
//   is skipped). Covers the multi-path accumulation contract for the same context.
// ===========================================================================
TEST_CASE("G2P-020: addPackagePath accumulates distinct paths for same context",
          "[g2p][edge]") {
    PackageManager pm;
    const auto v1 = stdc::VersionNumber::fromString("1.0.0").value();
    const auto dir1 = makeTempDir("g2p020-a");
    const auto dir2 = makeTempDir("g2p020-b");

    auto exp1 = pm.addPackagePath("edge_ctx", v1, dir1);
    REQUIRE(exp1.hasValue());
    auto exp2 = pm.addPackagePath("edge_ctx", v1, dir2);
    REQUIRE(exp2.hasValue());

    const auto paths = pm.packagePaths("edge_ctx", v1);
    REQUIRE(paths.size() == 2);

    std::filesystem::remove_all(dir1);
    std::filesystem::remove_all(dir2);
}

// ===========================================================================
// G2P-021: PhonemeDict load with consecutive spaces in values
//   "hello\ta  b\n" (two spaces between a and b) must yield exactly ["a","b"]
//   with no empty strings between them. Regression: previously the splitter
//   could emit empty tokens for consecutive delimiters.
// ===========================================================================
TEST_CASE("G2P-021: PhonemeDict load with consecutive spaces in values",
          "[g2p][edge]") {
    const auto dir = makeTempDir("g2p021-connspaces");
    const auto file = dir / "dict.txt";
    writeFile(file, "hello\ta  b\n"); // two spaces between a and b

    PhonemeDict dict;
    std::error_code ec;
    REQUIRE(dict.load(file, &ec));
    REQUIRE(!ec);

    const auto phonemes = dict["hello"].vec<std::string_view>();
    REQUIRE(phonemes.size() == 2);
    REQUIRE(phonemes[0] == "a");
    REQUIRE(phonemes[1] == "b");
    for (const auto &p : phonemes) {
        REQUIRE(!p.empty());
    }

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// G2P-022: PhonemeDict load with trailing space in values
//   "hello\ta b \n" (trailing space before newline) must yield exactly
//   ["a","b"] rather than ["a","b",""]. Regression: trailing delimiter
//   previously produced a spurious empty token.
// ===========================================================================
TEST_CASE("G2P-022: PhonemeDict load with trailing space in values",
          "[g2p][edge]") {
    const auto dir = makeTempDir("g2p022-trailspace");
    const auto file = dir / "dict.txt";
    writeFile(file, "hello\ta b \n");

    PhonemeDict dict;
    std::error_code ec;
    REQUIRE(dict.load(file, &ec));
    REQUIRE(!ec);

    const auto phonemes = dict["hello"].vec<std::string_view>();
    REQUIRE(phonemes.size() == 2);
    REQUIRE(phonemes[0] == "a");
    REQUIRE(phonemes[1] == "b");

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// G2P-023: PhonemeDict load with leading space in values
//   "hello\t a b\n" (leading space after tab) must yield exactly ["a","b"]
//   rather than ["","a","b"]. Regression: leading delimiter previously
//   produced a spurious empty token at the front.
// ===========================================================================
TEST_CASE("G2P-023: PhonemeDict load with leading space in values",
          "[g2p][edge]") {
    const auto dir = makeTempDir("g2p023-leadspace");
    const auto file = dir / "dict.txt";
    writeFile(file, "hello\t a b\n");

    PhonemeDict dict;
    std::error_code ec;
    REQUIRE(dict.load(file, &ec));
    REQUIRE(!ec);

    const auto phonemes = dict["hello"].vec<std::string_view>();
    REQUIRE(phonemes.size() == 2);
    REQUIRE(phonemes[0] == "a");
    REQUIRE(phonemes[1] == "b");

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// G2P-024: PhonemeDict load with single value (no spaces)
//   "hello\tworld\n" must yield exactly ["world"]. Covers the single-token
//   path to ensure the splitter does not over-trim or drop a lone value.
// ===========================================================================
TEST_CASE("G2P-024: PhonemeDict load with single value (no spaces)",
          "[g2p][edge]") {
    const auto dir = makeTempDir("g2p024-single");
    const auto file = dir / "dict.txt";
    writeFile(file, "hello\tworld\n");

    PhonemeDict dict;
    std::error_code ec;
    REQUIRE(dict.load(file, &ec));
    REQUIRE(!ec);

    const auto phonemes = dict["hello"].vec<std::string_view>();
    REQUIRE(phonemes.size() == 1);
    REQUIRE(phonemes[0] == "world");

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// G2P-025: PhonemeDict load with empty values (tab followed by newline)
//   "hello\t\n" (tab then newline, no values) must yield an empty PhonemeList
//   (0 entries). Regression: the splitter previously could emit a single empty
//   token for an empty value field.
// ===========================================================================
TEST_CASE("G2P-025: PhonemeDict load with empty values", "[g2p][edge]") {
    const auto dir = makeTempDir("g2p025-empty");
    const auto file = dir / "dict.txt";
    writeFile(file, "hello\t\n");

    PhonemeDict dict;
    std::error_code ec;
    REQUIRE(dict.load(file, &ec));
    REQUIRE(!ec);

    const auto phonemes = dict["hello"].vec<std::string_view>();
    REQUIRE(phonemes.empty());

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// G2P-026: PhonemeDict load with multiple consecutive spaces and trailing spaces
//   "hello\t  a   b  \n" must yield exactly ["a","b"]. Combines the
//   consecutive-delimiter and trailing-delimiter regressions into one case.
// ===========================================================================
TEST_CASE("G2P-026: PhonemeDict load with mixed consecutive and trailing spaces",
          "[g2p][edge]") {
    const auto dir = makeTempDir("g2p026-mixed");
    const auto file = dir / "dict.txt";
    writeFile(file, "hello\t  a   b  \n");

    PhonemeDict dict;
    std::error_code ec;
    REQUIRE(dict.load(file, &ec));
    REQUIRE(!ec);

    const auto phonemes = dict["hello"].vec<std::string_view>();
    REQUIRE(phonemes.size() == 2);
    REQUIRE(phonemes[0] == "a");
    REQUIRE(phonemes[1] == "b");
    for (const auto &p : phonemes) {
        REQUIRE(!p.empty());
    }

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// G2P-027: PhonemeDict load with CRLF line endings and spaces
//   "hello\ta b\r\n" must yield exactly ["a","b"]. The parser must treat \r\n
//   as a line terminator and not leave a trailing \r attached to the last
//   phoneme. The file is written in binary mode so the CRLF bytes are exact
//   regardless of host platform.
// ===========================================================================
TEST_CASE("G2P-027: PhonemeDict load with CRLF line endings and spaces",
          "[g2p][edge]") {
    const auto dir = makeTempDir("g2p027-crlf");
    const auto file = dir / "dict.txt";
    std::filesystem::create_directories(dir);
    {
        std::ofstream out(file, std::ios::binary);
        out << "hello\ta b\r\n";
    }

    PhonemeDict dict;
    std::error_code ec;
    REQUIRE(dict.load(file, &ec));
    REQUIRE(!ec);

    const auto phonemes = dict["hello"].vec<std::string_view>();
    REQUIRE(phonemes.size() == 2);
    REQUIRE(phonemes[0] == "a");
    REQUIRE(phonemes[1] == "b");

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// G2P-028: PhonemeDict load from non-existent file returns false
//   load() with a path that does not exist must return false and set the
//   outgoing error_code (no crash, no exception). Covers the error path of
//   the file-open step in PhonemeDict::load.
// ===========================================================================
TEST_CASE("G2P-028: PhonemeDict load from non-existent file returns false",
          "[g2p][edge]") {
    PhonemeDict dict;
    std::error_code ec;
    const auto missing = std::filesystem::temp_directory_path() /
                         ("srt-g2p-edge-missing-" +
                          std::to_string(
                              std::chrono::steady_clock::now().time_since_epoch().count()) +
                          ".txt");
    REQUIRE(!dict.load(missing, &ec));
    REQUIRE(ec);
}

// ===========================================================================
// G2P-029: createModuleTask sets Task::Mgr pointer (regression)
//   PackageManager::createModuleTask() must call task->setMgr(this) before
//   task->initialize(), otherwise chainG2p's ModelStep cannot find the
//   PackageManager (and the ONNX g2p task) via m_task->Mgr(). This test
//   verifies that after full Manager initialization, a ChainG2p task's
//   getObject() succeeds (which internally requires Mgr() to be non-null).
//
//   Requires Manager to be initialized with real G2P packages (same
//   precondition as G2P-009). SKIPs when the Manager is not initialized.
// ===========================================================================
TEST_CASE("G2P-029: ChainG2p task Mgr pointer is set after initialization",
          "[g2p][edge][mgrsmoke]") {
    auto *mgr = Manager::instance();
    if (!mgr->initialized()) {
        SKIP("Manager not initialized — setMgr integration cannot be verified");
    }

    // Try retrieving a ChainG2p task (eng-official, deu-official, etc.)
    // If any ChainG2p task exists, verify getObject() works (requires Mgr).
    bool anyChainTaskFound = false;
    for (const auto &chainId : {"g2p-eng-official", "g2p-deu-official",
                                 "g2p-fra-official", "g2p-ita-official"}) {
        auto taskExp = mgr->task(kG2pCategory, kOfficialContext, {}, chainId);
        if (!taskExp)
            continue;
        anyChainTaskFound = true;
        auto task = taskExp.take();
        REQUIRE(task);

        // getObject() internally calls Mgr(). If Mgr is null, it returns
        // "manager is not available" (ErrorCode::G2pRuntimeError).
        // If Mgr is set but the target is not found, it returns a different
        // error (e.g. "could not find category").
        auto objExp = task->getObject(kG2pCategory, "g2p-multig2p-multi-official");
        if (objExp)
            INFO("getObject succeeded: Mgr pointer is valid");
        else
            INFO("getObject failed (expected if multig2p not loaded): "
                 << objExp.error().message());
        // The key assertion: the error must NOT be "manager is not available".
        // That would mean setMgr was not called (the bug).
        if (!objExp) {
            CHECK(objExp.error().message().find("manager is not available") ==
                  std::string::npos);
        }
    }
    if (!anyChainTaskFound) {
        WARN("No ChainG2p task found — setMgr integration partially verified");
    }
}
