// tst_multig2p_languages.cpp
// Multig2p plugin integration tests covering all 9 default languages
// supported by Phonetic-Suite-Multi/modules/Multig2p-Multi.
//
// Verifies the chainG2p → multig2p call path by exercising the multig2p
// task directly with G2pInputV1 (languageId set per language). This
// catches the driver-lookup regression where multig2p fell back to copy
// mode ("unknown token <lyric>" downstream) because the ONNX driver
// factory was looked up via runtime->moduleCategory() instead of
// Manager::instance()->category().
//
// Also exercises the shipped ChainG2p packages Phonetic-Suite-Eng and
// Phonetic-Suite-Jpn end-to-end through the G2P Manager (the same path a
// real voicebank uses): real config.json tagger regexes + real dictionary
// files. These are regression tests for package DATA bugs:
//   - eng: the mixed-case tagger regex used to FullMatch only pure
//     letters, so words containing ' or - ("don't", "e-mail") fell into
//     copy mode and were never looked up in ds_cmudict-07b.txt.
//   - jpn: the kana tagger regex never matched (RE2 nested-class
//     construct), so kana fell into copy mode; and ん was mapped to the
//     uppercase phoneme name "N", which downstream phoneme dictionaries
//     do not recognize (now lowercase "n").
//
// Requires (gated by SYNTHRT_BUILD_TESTS, as all tests are):
//   - srt-driver-onnx plugin DLL + ONNX Runtime DLLs
//   - Multig2p + ChainG2p plugin DLLs
//   - Phonetic-Suite-Multi package (ONNX models + vocabulary)
//   - Phonetic-Suite-Eng / Phonetic-Suite-Jpn packages (dict + config)
// When the onnx environment is unavailable, the fixture reports
// setupError and every test SKIPs gracefully.
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
#include <synthrt/Core/Support/Logging.h>
#include <synthrt/Driver/OnnxSetup.h>
#include <synthrt/Driver/onnx/OnnxDriverApi.h>
#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/Core/Manager.h>
#include <synthrt/G2P/G2pOnnxSetup.h>
#include <synthrt/G2P/Task/G2pTask.h>
#include <synthrt/G2P/Task/Task.h>
#include <synthrt/SVS/InferenceContrib.h>

#ifndef SYNTHRT_TEST_SOURCE_DIR
#define SYNTHRT_TEST_SOURCE_DIR "."
#endif

#ifndef SYNTHRT_TEST_BINARY_DIR
#define SYNTHRT_TEST_BINARY_DIR "."
#endif

namespace fs = std::filesystem;

namespace {

    /// The "inference" module category (required by
    /// setupOnnxInferenceDriver) is installed by a static registrar inside
    /// the srt-svs DLL. Merely linking srt::svs provides no referenced
    /// symbol, so the linker drops the import and the registrar never runs
    /// (setupOnnxInferenceDriver then fails with "inference module category
    /// is not available"). Punning the address of one exported member into
    /// a volatile static keeps the import alive; the DLL is loaded at
    /// process start and its static initializer registers the category
    /// before the Runtime is constructed.
#if defined(_MSC_VER)
    union SvsImportAnchor {
        using ClassNameFn = const std::string &(srt::svs::InferenceSpec::*)() const;
        ClassNameFn call;
        void *address;
    };
    static volatile void *g_svsImportAnchor = []() -> void * {
        SvsImportAnchor svsAnchor;
        svsAnchor.call = &srt::svs::InferenceSpec::className;
        return svsAnchor.address;
    }();
#endif

    /// Locate the plugin root directory (containing srt-driver/, srt-g2p/,
    /// diffsinger/ subdirs with plugin descriptors and DLLs). In the build
    /// tree this is <binary_dir>/lib/plugins (see tools/RuntimeLayout.h).
    fs::path findPluginRoot() {
        const auto candidate = fs::path(SYNTHRT_TEST_BINARY_DIR) / "lib" / "plugins";
        if (fs::is_directory(candidate / "srt-g2p" / "G2ps" / "multig2p") &&
            fs::is_directory(candidate / "srt-driver" / "inferencedrivers" / "srt-onnxdriver")) {
            return candidate;
        }
        // Fallback: search upward from the binary dir.
        auto dir = fs::path(SYNTHRT_TEST_BINARY_DIR);
        for (int i = 0; i < 5; ++i) {
            const auto p = dir / "lib" / "plugins";
            if (fs::is_directory(p / "srt-g2p" / "G2ps" / "multig2p") &&
                fs::is_directory(p / "srt-driver" / "inferencedrivers" / "srt-onnxdriver")) {
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

            srt::core::Logger::setLogCallback(
                [](int level, const srt::core::LogContext &ctx, const std::string_view &msg) {
                    (void)level;
                    std::fprintf(stderr, "[g2p-test:%s] %.*s\n", ctx.category,
                                 static_cast<int>(msg.size()), msg.data());
                });

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
            //    G2P plugin search path: the category dir containing the
            //    plugin subdirs (each holding its own plugin.json), as in
            //    dsinfer-cli's defaultPluginPaths.
            const std::vector<fs::path> g2pPluginPaths = {
                pluginRoot / "srt-g2p" / "G2ps",
            };
            auto g2pDriverExp = srt::g2p::setupG2pOnnxDriver(runtime, g2pPluginPaths);
            if (!g2pDriverExp) {
                setupError = "setupG2pOnnxDriver failed: " + g2pDriverExp.error().message();
                return;
            }

            // 3. Register the official G2P package container (default context).
            //    Package dirs are discovered as SUBDIRECTORIES holding their
            //    own package.json, so the registered root is the container
            //    (resources/G2pPackages), matching dsinfer-cli's
            //    defaultG2pPackagePaths(): Phonetic-Suite-Multi (multig2p)
            //    + the chain packages the chain tests below exercise.
            auto *mgr = srt::g2p::Manager::instance();
            auto regExp = mgr->addPackagePath(srt::g2p::kOfficialContext,
                                              stdc::VersionNumber{},
                                              g2pPackagesRoot);
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

    /// Look up a G2P task by module id (multig2p or a chain package task)
    /// from the Manager's default context.
    srt::core::Expected<srt::core::NO<srt::g2p::Task>> getG2pTask(const std::string &taskId) {
        return srt::g2p::Manager::instance()->task(
            srt::g2p::kG2pCategory,
            srt::g2p::kOfficialContext,
            stdc::VersionNumber{},
            taskId);
    }

    /// Run G2P for a single word with the given languageId.
    struct G2pTestResult {
        std::string pronunciation;
        std::string mode;
        srt::g2p::G2pErrorType errorType;
        bool ok;
        std::string errorMessage;
    };

    /// Run a (chain) G2P task over one word and collect the per-word result.
    /// `languageId` is only forwarded to the task when non-empty.
    srt::core::Expected<G2pTestResult> runG2pWithTask(const std::string &word,
                                                       const std::string &taskId,
                                                       const std::string &languageId = {}) {
        auto taskExp = getG2pTask(taskId);
        if (!taskExp) {
            return taskExp.takeError();
        }
        auto task = taskExp.take();

        auto input = srt::core::NO<srt::g2p::G2pInputV1>::create();
        input->g2pInput = {word};
        if (!languageId.empty()) {
            input->languageId = languageId;
        }

        auto resultExp = task->start(input);
        if (!resultExp) {
            return resultExp.takeError();
        }

        auto result = resultExp.take();
        const auto g2pResult = result.as<srt::g2p::G2pResultV1>();
        if (!g2pResult || g2pResult->g2pResult.empty()) {
            return srt::core::Error(srt::core::ErrorCode::Unknown,
                                    "empty G2pResultV1 for word='" + word +
                                        "' taskId='" + taskId + "'");
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

    srt::core::Expected<G2pTestResult> runG2p(const std::string &word,
                                                const std::string &languageId) {
        return runG2pWithTask(word, "g2p-multig2p-multi-official", languageId);
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

    /// 9 default languages supported by Multig2p-Multi, each with the
    /// simplest common word to minimize ONNX variance. Language codes are
    /// ISO 639-3; internal key uses xxx/default.
    /// expectedPron from ChainG2p-*/...txt dicts + config.json phoneme sets.
    /// ita/default preprocessor remove_tone_digits strips stress (i1 → i).
    const std::vector<LangCase> &langCases() {
        static const std::vector<LangCase> cases = {
            // expectedPron = actual output of the shipped Multig2p-Multi
            // bundle (vocab_hash 913dcb42ff459d07), trailing space trimmed.
            {"eng/default", "hello", "eng", "hh eh l ow"},
            {"deu/default", "ja",    "deu", "y aa"},
            {"fra/default", "oui",   "fra", "ou ii"},
            {"ita/default", "si",    "ita", "s i"},
            {"kor/default", "가",    "kor", "v d eu"},
            {"por/default", "sim",   "por", "s i~"},
            {"rus/default", "да",    "rus", "v y"},
            {"spa/default", "si",    "spa", "s i"},
            {"fil/default", "oo",    "fil", "q o q o"},
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

    auto taskExp = getG2pTask("g2p-multig2p-multi-official");
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
            // The model emits a trailing space after each token; trim it
            // before comparing (expectedPron has no trailing space).
            if (!lc.expectedPron.empty()) {
                std::string pron = result.pronunciation;
                while (!pron.empty() && pron.back() == ' ')
                    pron.pop_back();
                REQUIRE(pron == lc.expectedPron);
            }
        }
    }
}

// ===========================================================================
// ChainG2p package data regressions, exercised through the real G2P Manager
// (the same path a voicebank uses): package registration, config.json tagger
// regexes and dictionary lookup all come from the shipped resources.
// ===========================================================================

// === eng chain: apostrophes and hyphens ===
// Regression: the ChainG2p-Eng mixed-case tagger regex was "(?i)([a-z]+)".
// RE2::FullMatch rejects any word containing ' or - ("don't", "e-mail"), so
// those words fell into copy mode and ds_cmudict-07b.txt was never consulted
// — producing "unknown token <lyric>" downstream. The regex now accepts
// internal apostrophes and hyphens.
//
// Hyphenated words not in the dictionary (e.g. "hello-world") are sent to the
// ONNX model as a WHOLE word (the model vocabulary contains the '-' token,
// see Multig2p-Multi/vocabulary.json "eng/default/-"), and the model emits a
// single phoneme sequence for the whole word — see ModelStep. Words that DO
// match the dictionary as full words (e.g. "x-ray") keep the dict path.

TEST_CASE("eng chain converts apostrophe and hyphen words through the manager",
          "[g2p][chain][eng][package-data]") {
    auto &f = fixture();
    if (!f.ready) {
        SKIP("L2 fixture not ready: " << f.setupError);
    }

    struct EngCase {
        std::string word;
        std::string expectedPron; // expected space-separated phonemes
    };
    const std::vector<EngCase> cases = {
        {"don't", "d ow n t"},
        {"e-mail", "iy m ey l"},
        {"it's", "ih t s"},
        {"x-ray", "eh k s r ey"},        // in dict: hyphen lookup, no model
        {"hello", "hh ax l ow"},         // plain word: dict hit
        {"hello-world", "hh eh l ow w er l d"}, // whole word through the model (hyphen token consumed by the model)
        {"hello--world", "hh eh l ow w er l d"}, // double hyphen: still one word, model path
        {"happy-birthday", "hh ae p iy b er th d ey"}, // both parts in dict: no model call at all
        {"hello-zzzzz", "hh eh l ow z z"}, // whole word through the model; OOV part pronounced by the model
        {"world", "w er l d"},           // hello-world part: dict path sanity
        {"zzzzz", "z eh z"},             // OOV: not in ds_cmudict, pure model path
    };

    for (const auto &c : cases) {
        DYNAMIC_SECTION("word='" << c.word << "'") {
            auto resultExp = runG2pWithTask(c.word, "g2p-eng-official");
            REQUIRE(resultExp);
            const auto result = resultExp.take();
            INFO("pronunciation='" << result.pronunciation << "' mode='"
                 << result.mode << "' errorType="
                 << static_cast<int>(result.errorType));
            REQUIRE(result.ok);
            REQUIRE(result.mode == srt::g2p::kG2pModeConvert);
            REQUIRE(result.pronunciation == c.expectedPron);
        }
    }
}

TEST_CASE("eng chain lowercases uppercase words via cleaner and hits the dict",
          "[g2p][chain][eng][package-data]") {
    auto &f = fixture();
    if (!f.ready) {
        SKIP("L2 fixture not ready: " << f.setupError);
    }

    // The chain is now "raw lookup -> cleaner -> re-lookup -> model": the
    // tagger no longer short-circuits uppercase words into copy mode. "NASA"
    // is looked up exactly as-is (miss), then the cleaner pass lowercases it
    // to "nasa", which IS in ds_cmudict-07b.txt ("nasa" -> "n ae s ax"), so
    // it converts instead of staying copy.
    auto resultExp = runG2pWithTask("NASA", "g2p-eng-official");
    REQUIRE(resultExp);
    const auto result = resultExp.take();
    INFO("pronunciation='" << result.pronunciation << "' mode='"
         << result.mode << "' errorType="
         << static_cast<int>(result.errorType));
    REQUIRE(result.ok);
    REQUIRE(result.mode == srt::g2p::kG2pModeConvert);
    REQUIRE(result.pronunciation == "n ae s ax");
}

// === eng chain: single uppercase letters ===
// Regression: the ChainG2p-Eng uppercase tagger was "([A-Z]+)", which also
// matched single uppercase letters ("I", "A") and put them in copy mode —
// they were never lowercased and never looked up in ds_cmudict-07b.txt.
// The uppercase tagger is now gone; single letters match the word tagger,
// the raw lookup misses, the chain's cleaner pass lowercases them, and they
// hit the dictionary ("i" -> "ay", "a" -> "ax").

TEST_CASE("eng chain converts single uppercase letters through the manager",
          "[g2p][chain][eng][package-data]") {
    auto &f = fixture();
    if (!f.ready) {
        SKIP("L2 fixture not ready: " << f.setupError);
    }

    struct EngCase {
        std::string word;
        std::string expectedPron;
    };
    const std::vector<EngCase> cases = {
        {"I", "ay"},
        {"A", "ax"},
        {"I'M", "ay m"},
    };

    for (const auto &c : cases) {
        DYNAMIC_SECTION("word='" << c.word << "'") {
            auto resultExp = runG2pWithTask(c.word, "g2p-eng-official");
            REQUIRE(resultExp);
            const auto result = resultExp.take();
            INFO("pronunciation='" << result.pronunciation << "' mode='"
                 << result.mode << "' errorType="
                 << static_cast<int>(result.errorType));
            REQUIRE(result.ok);
            REQUIRE(result.mode == srt::g2p::kG2pModeConvert);
            REQUIRE(result.pronunciation == c.expectedPron);
        }
    }
}

// === eng chain: hyphen words go through the model as ONE whole word ===
// Whole-word semantics in ModelStep: "hello-''" is sent to the ONNX model
// as a single unit (no part splitting/merging). The model vocabulary
// contains the apostrophe/hyphen tokens (Multig2p-Multi/vocabulary.json
// "eng/default/-", "eng/default/'"), so the model consumes them and emits
// one phoneme sequence for the whole word — exactly like any other OOV word.

TEST_CASE("eng chain converts the whole hyphen word through the model",
          "[g2p][chain][eng][package-data]") {
    auto &f = fixture();
    if (!f.ready) {
        SKIP("L2 fixture not ready: " << f.setupError);
    }

    auto resultExp = runG2pWithTask("hello-''", "g2p-eng-official");
    REQUIRE(resultExp);
    const auto result = resultExp.take();
    INFO("pronunciation='" << result.pronunciation << "' mode='"
         << result.mode << "' errorType="
         << static_cast<int>(result.errorType));
    REQUIRE(result.ok);
    REQUIRE(result.mode == srt::g2p::kG2pModeConvert);
    REQUIRE(result.errorType == srt::g2p::NoError);
    REQUIRE(result.pronunciation == "hh eh l ow z");
}

// === non-eng chain languages: lowercase cleaner + two-pass dict ===
// The ChainG2p-Deu/Fra/Ita/Por/Rus/Spa packages follow Eng's chain shape
// ("raw lookup -> cleaner -> re-lookup -> model"). Corpus lyrics often
// arrive capitalized ("Ja", "Oui"), but the *_dict.txt files (like
// ds_cmudict-07b.txt) are lowercase-only, and DictStep no longer applies
// tolower internally. The cleaner pass lowercases the word and the second
// dict pass then resolves it, so capitalized words convert to phonemes
// instead of falling to the model (or copy).

TEST_CASE("non-eng chains lowercase capitalized words via cleaner and hit the dict",
          "[g2p][chain][package-data]") {
    auto &f = fixture();
    if (!f.ready) {
        SKIP("L2 fixture not ready: " << f.setupError);
    }

    struct ChainCase {
        std::string taskId;
        std::string word;       // capitalized input
        std::string expectedPron; // expected space-separated phonemes (dict hit)
    };
    // Expected values are the exact *_dict.txt entries (dict path, no model).
    const std::vector<ChainCase> cases = {
        {"g2p-deu-official", "JA",    "y aa"},      // "ja" in deu_dict.txt
        {"g2p-fra-official", "OUI",   "ou ii"},     // "oui" in fra_dict.txt
        {"g2p-ita-official", "ABITA", "a b i t a"}, // "abita" in ita_dict.txt
        {"g2p-por-official", "SIM",   "s i~"},      // "sim" in por_dict.txt
        {"g2p-rus-official", "НЕТ",   "nn je t"},   // "нет" in rus_dict.txt
        {"g2p-spa-official", "SI",    "s i"},       // "si" in spa_dict.txt
    };

    for (const auto &c : cases) {
        DYNAMIC_SECTION(c.taskId << " word='" << c.word << "'") {
            auto resultExp = runG2pWithTask(c.word, c.taskId);
            REQUIRE(resultExp);
            const auto result = resultExp.take();
            INFO("pronunciation='" << result.pronunciation << "' mode='"
                 << result.mode << "' errorType="
                 << static_cast<int>(result.errorType));
            REQUIRE(result.ok);
            REQUIRE(result.mode == srt::g2p::kG2pModeConvert);
            REQUIRE(result.pronunciation == c.expectedPron);
        }
    }
}

// === deu chain: '-' token absent from the deu vocabulary → whole word fails ===
// The multig2p vocabulary assigns the '-' symbol per language ({lang}/default/-);
// deu (like ita/kor) has no such token. Sending "hello-world" with deu/default
// would silently encode '-' as <unk> and produce garbage phonemes, so the whole
// hyphenated word must fail instead (PhonemeGenerationFailed + original lyric),
// never a partial or fabricated sequence.

TEST_CASE("deu chain fails hyphen words because deu vocab has no '-' token",
          "[g2p][chain][deu][package-data]") {
    auto &f = fixture();
    if (!f.ready) {
        SKIP("L2 fixture not ready: " << f.setupError);
    }

    auto resultExp = runG2pWithTask("hello-world", "g2p-deu-official");
    REQUIRE(resultExp);
    const auto result = resultExp.take();
    INFO("pronunciation='" << result.pronunciation << "' mode='"
         << result.mode << "' errorType="
         << static_cast<int>(result.errorType));
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.errorType == srt::g2p::PhonemeGenerationFailed);
    REQUIRE(result.pronunciation == "hello-world");
}

// === jpn chain: kana must reach kana2romaji.txt; ん → lowercase n ===
// Regression 1: the kana tagger regex used a RE2 nested-class construct that
// compiled but never matched, so every kana fell into copy mode and the
// dictionary was never consulted.
// Regression 2: ん was mapped to the uppercase phoneme name "N", which the
// downstream phoneme dictionaries do not recognize; it now maps to "n".

TEST_CASE("jpn chain converts kana to romaji phonemes through the manager",
          "[g2p][chain][jpn][package-data]") {
    auto &f = fixture();
    if (!f.ready) {
        SKIP("L2 fixture not ready: " << f.setupError);
    }

    struct JpnCase {
        std::string word;      // UTF-8 kana
        std::string expectedPron;
    };
    const std::vector<JpnCase> cases = {
        {"ん", "n"},   // moraic nasal: must NOT be "N"
        {"こ", "ko"},
        {"っ", "cl"},  // sokuon
        {"しゃ", "sha"}, // yōon
    };

    for (const auto &c : cases) {
        DYNAMIC_SECTION("word='" << c.word << "'") {
            auto resultExp = runG2pWithTask(c.word, "g2p-jpn-official");
            REQUIRE(resultExp);
            const auto result = resultExp.take();
            INFO("pronunciation='" << result.pronunciation << "' mode='"
                 << result.mode << "' errorType="
                 << static_cast<int>(result.errorType));
            REQUIRE(result.ok);
            REQUIRE(result.mode == srt::g2p::kG2pModeConvert);
            REQUIRE(result.pronunciation == c.expectedPron);
        }
    }
}

TEST_CASE("jpn chain keeps latin words in copy mode",
          "[g2p][chain][jpn][package-data]") {
    auto &f = fixture();
    if (!f.ready) {
        SKIP("L2 fixture not ready: " << f.setupError);
    }

    // Latin input matches the latin tagger (copy): words stay untouched,
    // they are not looked up in the kana dictionary.
    auto resultExp = runG2pWithTask("hello", "g2p-jpn-official");
    INFO("g2p call error: " << (resultExp ? std::string("(ok)") : resultExp.error().message()));
    REQUIRE(resultExp);
    const auto result = resultExp.take();
    REQUIRE(result.mode == srt::g2p::kG2pModeCopy);
}
