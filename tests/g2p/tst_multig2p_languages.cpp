// tst_multig2p_languages.cpp
// Multig2p plugin integration tests covering all languages supported by
// Phonetic-Suite-Multi/modules/Multig2p-Multi.
//
// Verifies the chainG2p → multig2p call path by exercising the multig2p
// task directly with G2pInputV1 (languageId set per language). This
// catches the driver-lookup regression where multig2p fell back to copy
// mode ("unknown token <lyric>" downstream) because the ONNX driver
// factory was looked up via runtime->moduleCategory() instead of
// Manager::instance()->category().
//
// Requires (gated by SYNTHRT_BUILD_TESTS):
//   - srt-driver-onnx plugin DLL + ONNX Runtime DLLs
//   - Multig2p plugin DLL
//   - Phonetic-Suite-Multi package (ONNX models + vocabulary)
//
// Word selection: each language uses the simplest, most common word to
// minimize ONNX inference variance and avoid sporadic failures. Language
// codes use ISO 639-3 three-letter format.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Driver/OnnxSetup.h>
#include <synthrt/Driver/onnx/OnnxDriverApi.h>
#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/Core/Manager.h>
#include <synthrt/G2P/G2pOnnxSetup.h>
#include <synthrt/G2P/Task/G2pTask.h>
#include <synthrt/G2P/Task/Task.h>

#ifndef SYNTHRT_TEST_SOURCE_DIR
#define SYNTHRT_TEST_SOURCE_DIR "."
#endif

#ifndef SYNTHRT_TEST_BINARY_DIR
#define SYNTHRT_TEST_BINARY_DIR "."
#endif

namespace fs = std::filesystem;

namespace {

    /// Locate the plugin root directory (containing G2P/, Driver/, diffsinger/
    /// subdirs with plugin descriptors). In the build tree this is
    /// <binary_dir>/plugins.
    fs::path findPluginRoot() {
        const auto candidate = fs::path(SYNTHRT_TEST_BINARY_DIR) / "plugins";
        if (fs::is_directory(candidate / "G2P" / "multig2p") &&
            fs::is_directory(candidate / "Driver" / "onnx")) {
            return candidate;
        }
        // Fallback: search upward from the binary dir.
        auto dir = fs::path(SYNTHRT_TEST_BINARY_DIR);
        for (int i = 0; i < 5; ++i) {
            const auto p = dir / "plugins";
            if (fs::is_directory(p / "G2P" / "multig2p") &&
                fs::is_directory(p / "Driver" / "onnx")) {
                return p;
            }
            dir = dir.parent_path();
        }
        return candidate;
    }

    /// Locate the G2P packages root (containing Phonetic-Suite-Multi).
    fs::path findG2pPackagesRoot() {
        // 1. Source tree: <source>/resources/G2pPackages
        const auto srcCandidate = fs::path(SYNTHRT_TEST_SOURCE_DIR) / "resources" / "G2pPackages";
        if (fs::is_directory(srcCandidate / "Phonetic-Suite-Multi")) {
            return srcCandidate;
        }
        // 2. Build tree: <binary>/share/synthrt/G2pPackages
        const auto buildCandidate = fs::path(SYNTHRT_TEST_BINARY_DIR) / "share" / "synthrt" / "G2pPackages";
        if (fs::is_directory(buildCandidate / "Phonetic-Suite-Multi")) {
            return buildCandidate;
        }
        return srcCandidate;
    }

    /// Global fixture: sets up Runtime + ONNX driver + G2P Manager once
    /// per test executable invocation. Manager::initialize() is idempotent
    /// (returns AlreadyInitialized on second call), so a static flag guards
    /// the setup.
    struct Multig2pL2Fixture {
        srt::core::Runtime runtime;
        fs::path pluginRoot;
        fs::path g2pPackagesRoot;
        bool ready = false;
        std::string setupError;

        Multig2pL2Fixture() {
            pluginRoot = findPluginRoot();
            g2pPackagesRoot = findG2pPackagesRoot();

            if (!fs::is_directory(pluginRoot)) {
                setupError = "plugin root not found: " + pluginRoot.string();
                return;
            }
            if (!fs::is_directory(g2pPackagesRoot / "Phonetic-Suite-Multi")) {
                setupError = "Phonetic-Suite-Multi not found under: " + g2pPackagesRoot.string();
                return;
            }

            // 1. Set up ONNX inference driver in the Runtime.
            srt::driver::OnnxDriverConfig driverCfg;
            driverCfg.ep = srt::driver::onnx::CPUExecutionProvider;
            driverCfg.deviceIndex = 0;
            auto driverExp = srt::driver::setupOnnxInferenceDriver(runtime, pluginRoot, driverCfg);
            if (!driverExp) {
                setupError = "setupOnnxInferenceDriver failed: " + driverExp.error().message();
                return;
            }

            // 2. Set up G2P ONNX driver (reuses the Runtime's dsdriver).
            //    G2P plugin search paths: the multig2p and chain plugin dirs.
            const std::vector<fs::path> g2pPluginPaths = {
                pluginRoot / "G2P" / "multig2p",
                pluginRoot / "G2P" / "chain",
            };
            auto g2pDriverExp = srt::g2p::setupG2pOnnxDriver(runtime, g2pPluginPaths);
            if (!g2pDriverExp) {
                setupError = "setupG2pOnnxDriver failed: " + g2pDriverExp.error().message();
                return;
            }

            // 3. Register Phonetic-Suite-Multi as official G2P (default context).
            auto *mgr = srt::g2p::Manager::instance();
            auto regExp = mgr->addPackagePath(srt::g2p::kOfficialContext,
                                               stdc::VersionNumber{},
                                               g2pPackagesRoot / "Phonetic-Suite-Multi");
            if (!regExp) {
                setupError = "addPackagePath failed: " + regExp.error().message();
                return;
            }

            // 4. Initialize the Manager (loads plugins, creates tasks, opens ONNX sessions).
            auto initExp = mgr->initialize();
            if (!initExp) {
                setupError = "Manager::initialize failed: " + initExp.error().message();
                return;
            }

            ready = true;
        }
    };

    /// Lazy singleton accessor — constructed on first use, lives until process exit.
    Multig2pL2Fixture &fixture() {
        static Multig2pL2Fixture f;
        return f;
    }

    /// Look up the multig2p task from the Manager.
    srt::core::Expected<srt::core::NO<srt::g2p::Task>> getMultig2pTask() {
        return srt::g2p::Manager::instance()->task(
            srt::g2p::kG2pCategory,
            srt::g2p::kOfficialContext,
            stdc::VersionNumber{},
            "g2p-multig2p-multi-official");
    }

    /// Run G2P for a single word with the given languageId.
    struct G2pTestResult {
        std::string pronunciation;
        std::string mode;
        srt::g2p::G2pErrorType errorType;
        bool ok;
        std::string errorMessage;
    };

    srt::core::Expected<G2pTestResult> runG2p(const std::string &word,
                                                const std::string &languageId) {
        auto taskExp = getMultig2pTask();
        if (!taskExp) {
            return taskExp.takeError();
        }
        auto task = taskExp.take();

        auto input = srt::core::NO<srt::g2p::G2pInputV1>::create();
        input->g2pInput = {word};
        input->languageId = languageId;

        auto resultExp = task->start(input);
        if (!resultExp) {
            return resultExp.takeError();
        }

        auto result = resultExp.take();
        const auto g2pResult = result.as<srt::g2p::G2pResultV1>();
        if (!g2pResult || g2pResult->g2pResult.empty()) {
            return srt::core::Error(srt::core::ErrorCode::Unknown,
                                    "empty G2pResultV1 for word='" + word +
                                        "' langId='" + languageId + "'");
        }

        const auto &res = g2pResult->g2pResult[0];
        return G2pTestResult{
            res.pronunciation,
            res.mode,
            res.errorType,
            res.isOk(),
            res.pronunciation.empty() ? "empty pronunciation" : ""
        };
    }

    /// Language test case: languageId + simple word + expected phoneme sequence.
    /// expectedPron (empty = only verify non-empty + non-copy; non-empty =
    /// verify exact phoneme sequence). Expected values derived from the
    /// vocabulary phoneme set for each language variant in
    /// modules/Multig2p-Multi/vocabulary.json.
    struct LangCase {
        std::string languageId;
        std::string word;
        std::string description;
        std::string expectedPron;  // expected space-separated phonemes
    };

    /// All 12 languages supported by Multig2p-Multi, each with the simplest
    /// common word to minimize ONNX variance. Language codes are ISO 639-3.
    const std::vector<LangCase> &langCases() {
        static const std::vector<LangCase> cases = {
            // eng/default uses CMU ARPABET: ah (not ax) for schwa
            {"eng/default",       "hello", "eng (CMU)",          "hh ah l ow"},
            // eng/plus (ARPABET Plus) uses ax for the reduced schwa
            {"eng/plus",          "hello", "eng (ARPABET Plus)", "hh ax l ow"},
            {"deu/default",       "ja",    "deu (OpenUTAU)",     ""},
            {"fra/default",       "oui",   "fra (OpenUTAU)",     ""},
            {"ita/default",       "si",    "ita (OpenUTAU)",     ""},
            {"kor/default",       "가",    "kor (OpenUTAU)",     ""},
            {"por/default",       "sim",   "por (OpenUTAU)",     ""},
            {"rus/default",       "да",    "rus (OpenUTAU)",     ""},
            {"spa/default",       "si",    "spa (OpenUTAU)",     ""},
            {"fil/default",       "oo",    "fil (OpenUTAU)",     ""},
            {"deu/marzipan",      "ja",    "deu (Marzipan)",     ""},
            {"fra/millefeuille",  "oui",   "fra (Millefeuille)", ""},
        };
        return cases;
    }

} // namespace

// === multig2p driver availability ===
// Regression test for the driver-lookup bug: multig2p must find the ONNX
// driver factory in Manager's kDriverCategory (registered by
// setupG2pOnnxDriver), not in Runtime's moduleCategory().

TEST_CASE("multig2p driver is available after setupG2pOnnxDriver", "[g2p][multig2p][driver]") {
    auto &f = fixture();
    if (!f.ready) {
        SKIP("L2 fixture not ready: " << f.setupError);
    }

    auto taskExp = getMultig2pTask();
    REQUIRE(taskExp);
    auto task = taskExp.take();
    REQUIRE(task);
}

// === multig2p inference per language ===
// Each language gets its own SECTION so failures are isolated and the
// language is reported in the test name.

TEST_CASE("multig2p inference produces phonemes per language", "[g2p][multig2p][lang]") {
    auto &f = fixture();
    if (!f.ready) {
        SKIP("L2 fixture not ready: " << f.setupError);
    }

    for (const auto &lc : langCases()) {
        DYNAMIC_SECTION(lc.languageId << " word='" << lc.word << "' (" << lc.description << ")") {
            auto resultExp = runG2p(lc.word, lc.languageId);
            REQUIRE(resultExp);
            const auto result = resultExp.take();

            // The result must not be a copy fallback (which would mean the
            // driver was not found or inference failed). This is the core
            // regression check: before the fix, mode was "copy" and
            // pronunciation == lyric, causing "unknown token" downstream.
            INFO("pronunciation='" << result.pronunciation << "' mode='" << result.mode
                 << "' errorType=" << static_cast<int>(result.errorType));
            REQUIRE(result.ok);
            REQUIRE(result.mode == srt::g2p::kG2pModeConvert);
            REQUIRE_FALSE(result.pronunciation.empty());
            // Pronunciation must differ from the input lyric (actual
            // grapheme-to-phoneme conversion happened, not a copy).
            REQUIRE(result.pronunciation != lc.word);
            // When expectedPron is set, verify exact phoneme sequence.
            // This guards against silent regression of the ONNX model output.
            if (!lc.expectedPron.empty()) {
                REQUIRE(result.pronunciation == lc.expectedPron);
            }
        }
    }
}
