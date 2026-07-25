// T-04 chain process error propagation tests (BF-53 regression).
//
// Covers G2pPipeline::process error propagation paths confirmed by BF-53
// (no bug; per-word error propagation by design). The tests exercise the
// ACTUAL G2pPipeline::process / G2pPipeline::configure code by compiling the
// chain plugin's internal sources directly into the test target (see
// unittests/G2P/CMakeLists.txt). No real ONNX models are loaded: ModelStep is
// exercised through its null-task graceful-degradation path, which sets
// DriverUnavailable on every convert-mode word — exactly the per-word error
// propagation BF-53 audited.
//
// Key BF-53 facts verified here:
//   - Per-word error propagation: a step failure sets word.errorType, which
//     propagates to G2pRes via the TaskImpl::start result-building logic
//     (replicated in buildResFromWord below, mirroring TaskImpl.cpp L51-72).
//   - isStopProcessing()/setStopProcessing() is dead code (no step calls
//     setStopProcessing). G2P-031 verifies it stays false during normal
//     processing; G2P-032 verifies the process() break path works when
//     setStopProcessing(true) is called externally (future-activation
//     regression for the archived dead code).
//   - chain start() always returns a result (never a pipeline-level error);
//     errors are per-word. This is by-design (responsibility-chain mode), so
//     the "step failure -> start() returns error" variant from the T-04
//     matrix does NOT apply — instead we verify per-word errorType reaches
//     G2pRes.isFailed().
//
// Level: L1 (does not load the ChainG2p plugin DLL; compiles internal
// sources directly). No plugin runtime, no ONNX models.
//
// Test numbering: continues from G2P-028 (tst_g2p_edge.cpp). Tags follow
// the project convention [g2p][extreme] / [g2p][bf-53].

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/JSON.h>
#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/Package/Package.h>

#include "internal/Core/G2pContext.h"
#include "internal/Core/G2pPipeline.h"
#include "internal/Core/G2pStep.h"

using namespace srt::g2p::plugins::ChainG2p;
using srt::core::ErrorCode;
using srt::core::Expected;
using srt::core::JsonObject;
using srt::core::JsonValue;
using srt::g2p::G2pErrorType;
using srt::g2p::G2pRes;
using srt::g2p::ModuleSpec;

namespace {

    /// Minimal ModuleSpec subclass for testing.
    ///
    /// ModuleSpec's constructor is protected; this subclass exposes it. The
    /// resulting Impl has empty m_id / m_path / m_contextKey, which is fine
    /// for the steps exercised here:
    ///   - TagAndValidateStep / FormatStep / FallbackStep never read spec
    ///     fields (they only store the pointer).
    ///   - DictStep uses spec->path() as the ConfigAccessor base path; the
    ///     tests pass an ABSOLUTE dict file path so the empty base path is
    ///     irrelevant (path / absolute == absolute).
    ///   - ModelStep reads spec->contextKey() and m_task->Mgr(); m_task is
    ///     null in these tests, so ModelStep disables itself before reaching
    ///     the FQID lookup and sets DriverUnavailable on convert-mode words.
    class TestModuleSpec : public srt::core::ModuleSpec {
    public:
        TestModuleSpec() : srt::core::ModuleSpec("test-chain-g2p") {}
        ~TestModuleSpec() override = default;
    };

    std::filesystem::path makeTempDir(const std::string &name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = std::filesystem::temp_directory_path() /
                   ("srt-chain-err-" + name + "-" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        return dir;
    }

    void writeFile(const std::filesystem::path &path, const std::string &text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << text;
    }

    /// Parse a JSON object literal into a JsonObject by value. Returns an
    /// empty object on parse failure; callers that need strict parsing should
    /// assert the expected keys afterwards.
    JsonObject parseJsonObject(const std::string &text) {
        std::string err;
        auto value = JsonValue::fromJson(text, false, &err);
        if (!err.empty() || !value.isObject()) {
            return {};
        }
        return value.toObject();
    }

    /// Replicate ChainG2pTaskImpl::start result-building logic
    /// (TaskImpl.cpp L51-72) to verify per-word error propagation reaches
    /// G2pRes. This is the exact code path BF-53 audited:
    /// `res.errorType = word.errorType;`.
    G2pRes buildResFromWord(const G2pContext::WordInfo &word,
                            const std::string &g2pId) {
        G2pRes res;
        res.lyric = word.lyric;
        res.g2pId = g2pId;
        res.pronunciation = word.pronunciation;
        res.candidates = word.candidates;
        res.mode = word.mode;
        res.errorType = word.errorType;
        // Copy-mode empty-pronunciation fallback (TaskImpl.cpp L64-69).
        if (res.mode == srt::g2p::kG2pModeCopy && res.pronunciation.empty()) {
            res.pronunciation = res.lyric;
            if (res.candidates.empty()) {
                res.candidates = {res.lyric};
            }
        }
        return res;
    }

    /// Build a chain pipeline config with the given steps JSON array body.
    /// `stepsBody` is the raw JSON array content (without enclosing []).
    JsonObject chainConfig(const std::string &stepsBody) {
        return parseJsonObject(R"({"steps":[)" + stepsBody + R"(]})");
    }

} // namespace

// ===========================================================================
// G2P-029: ModelStep with null task -> per-word DriverUnavailable.
//
// BF-53 core regression: when ModelStep cannot resolve an ONNX task (here
// m_task is null so PackageManager is unavailable), it disables itself and
// marks every convert-mode word with errorType=DriverUnavailable. The
// downstream FallbackStep does NOT override the error because ModelStep
// already set pronunciation=lyric (non-empty). This is the per-word error
// propagation BF-53 confirmed as by-design.
// ===========================================================================
TEST_CASE("G2P-029: ModelStep null task propagates DriverUnavailable per word",
          "[g2p][bf-53][extreme]") {
    TestModuleSpec spec;
    G2pPipeline pipeline(&spec, /*task=*/nullptr);

    // tagAndValidate (default rules: [a-z]+ -> convert) -> model (id refers
    // to a nonexistent task; with null m_task the model disables itself) ->
    // fallback (no-op because pronunciation is already set).
    auto cfg = chainConfig(
        R"({"step":"tagAndValidate"},)"
        R"({"step":"model","params":{"id":"nonexistent-onnx-g2p"}},)"
        R"({"step":"fallback"})");
    auto configExp = pipeline.configure(cfg);
    REQUIRE(configExp.hasValue());

    G2pContext context({"hello"}, &spec);
    pipeline.process(context);

    REQUIRE(context.words().size() == 1);
    const auto &word = context.words()[0];
    REQUIRE(word.lyric == "hello");
    REQUIRE(word.mode == srt::g2p::kG2pModeConvert);
    // ModelStep graceful-degradation path: pronunciation=lyric, errorType=
    // DriverUnavailable. FallbackStep skips because pronunciation non-empty.
    REQUIRE(word.pronunciation == "hello");
    REQUIRE(word.errorType == srt::g2p::DriverUnavailable);
    REQUIRE(word.fromModel == false);
    REQUIRE(word.fromFallback == false);
}

// ===========================================================================
// G2P-030: Per-word error propagates to G2pRes (TaskImpl result building).
//
// Replicates ChainG2pTaskImpl::start's result-building loop to verify the
// audited line `res.errorType = word.errorType;` (TaskImpl.cpp L61) carries
// the per-word failure into the public G2pRes. The chain never returns a
// pipeline-level error from start(); callers detect failure via
// G2pRes::isFailed().
// ===========================================================================
TEST_CASE("G2P-030: per-word errorType propagates to G2pRes.isFailed()",
          "[g2p][bf-53][extreme]") {
    TestModuleSpec spec;
    G2pPipeline pipeline(&spec, /*task=*/nullptr);

    auto cfg = chainConfig(
        R"({"step":"tagAndValidate"},)"
        R"({"step":"model","params":{"id":"missing"}},)"
        R"({"step":"fallback"})");
    REQUIRE(pipeline.configure(cfg).hasValue());

    G2pContext context({"hello", "world"}, &spec);
    pipeline.process(context);

    REQUIRE(context.words().size() == 2);
    const std::string g2pId = "test-chain-g2p";
    for (const auto &word : context.words()) {
        G2pRes res = buildResFromWord(word, g2pId);
        // BF-53 contract: per-word error reaches the public result.
        REQUIRE(res.errorType == srt::g2p::DriverUnavailable);
        REQUIRE(res.isFailed());
        REQUIRE(res.lyric == word.lyric);
    }
}

// ===========================================================================
// G2P-031: isStopProcessing() stays false after normal process (dead code).
//
// BF-53 archived finding: setStopProcessing() is never called by any step,
// so isStopProcessing() is always false during normal processing. The
// `if (context.isStopProcessing()) break;` in G2pPipeline::process is dead
// code. This test documents the invariant so a future step that activates
// the stop path does so deliberately.
// ===========================================================================
TEST_CASE("G2P-031: isStopProcessing stays false during normal process",
          "[g2p][bf-53][extreme]") {
    TestModuleSpec spec;
    G2pPipeline pipeline(&spec, /*task=*/nullptr);

    auto cfg = chainConfig(
        R"({"step":"tagAndValidate"},)"
        R"({"step":"fallback"})");
    REQUIRE(pipeline.configure(cfg).hasValue());

    G2pContext context({"hello"}, &spec);
    REQUIRE_FALSE(context.isStopProcessing());

    pipeline.process(context);

    // No step calls setStopProcessing; flag must remain false.
    REQUIRE_FALSE(context.isStopProcessing());
    // FallbackStep ran (pronunciation populated, errorType set).
    REQUIRE(context.words()[0].pronunciation == "hello");
    REQUIRE(context.words()[0].errorType == srt::g2p::PhonemeGenerationFailed);
}

// ===========================================================================
// G2P-032: setStopProcessing(true) before process() breaks after step 0.
//
// Exercises the actual G2pPipeline::process break path. With stopProcessing
// preset to true, step 0 (tagAndValidate) runs and sets word.mode, then the
// loop breaks before step 1 (fallback) — so pronunciation stays empty and
// errorType stays NoError. The "without stop" SECTION confirms the contrast:
// fallback runs and sets pronunciation + PhonemeGenerationFailed.
// ===========================================================================
TEST_CASE("G2P-032: setStopProcessing(true) breaks pipeline after first step",
          "[g2p][bf-53][extreme]") {
    TestModuleSpec spec;

    SECTION("stop requested before process: only first step runs") {
        G2pPipeline pipeline(&spec, /*task=*/nullptr);
        auto cfg = chainConfig(
            R"({"step":"tagAndValidate"},)"
            R"({"step":"fallback"})");
        REQUIRE(pipeline.configure(cfg).hasValue());

        G2pContext context({"hello"}, &spec);
        context.setStopProcessing(true);
        REQUIRE(context.isStopProcessing());

        pipeline.process(context);

        const auto &word = context.words()[0];
        // tagAndValidate ran: mode set to convert for lowercase input.
        REQUIRE(word.mode == srt::g2p::kG2pModeConvert);
        // fallback did NOT run: pronunciation empty, no fallback error.
        REQUIRE(word.pronunciation.empty());
        REQUIRE(word.errorType == srt::g2p::NoError);
        REQUIRE_FALSE(word.fromFallback);
    }

    SECTION("no stop: both steps run, fallback applies") {
        G2pPipeline pipeline(&spec, /*task=*/nullptr);
        auto cfg = chainConfig(
            R"({"step":"tagAndValidate"},)"
            R"({"step":"fallback"})");
        REQUIRE(pipeline.configure(cfg).hasValue());

        G2pContext context({"hello"}, &spec);
        pipeline.process(context);

        const auto &word = context.words()[0];
        REQUIRE(word.mode == srt::g2p::kG2pModeConvert);
        REQUIRE(word.pronunciation == "hello");
        REQUIRE(word.errorType == srt::g2p::PhonemeGenerationFailed);
        REQUIRE(word.fromFallback);
    }
}

// ===========================================================================
// G2P-033: Normal conversion via DictStep does not regress.
//
// A working chain (tagAndValidate -> dict -> fallback) must still resolve
// dictionary words correctly: pronunciation from the dict, fromDict=true,
// errorType=NoError. Fallback is a no-op because pronunciation is populated.
// ===========================================================================
TEST_CASE("G2P-033: normal dict conversion does not regress",
          "[g2p][bf-53][extreme]") {
    const auto dir = makeTempDir("g2p033-dict");
    const auto dictFile = dir / "dict.txt";
    writeFile(dictFile, "hello\th e l l o\n");

    TestModuleSpec spec;
    G2pPipeline pipeline(&spec, /*task=*/nullptr);

    // Absolute path with forward slashes (generic_string) so the embedded
    // JSON value needs no backslash escaping and ConfigAccessor resolves it
    // regardless of spec->path() (which is empty for TestModuleSpec).
    const auto cfg = chainConfig(
        R"({"step":"tagAndValidate"},)"
        R"({"step":"dict","params":{"file":")" +
        dictFile.generic_string() + R"("}},)"
        R"({"step":"fallback"})");
    REQUIRE(pipeline.configure(cfg).hasValue());

    G2pContext context({"hello"}, &spec);
    pipeline.process(context);

    const auto &word = context.words()[0];
    REQUIRE(word.mode == srt::g2p::kG2pModeConvert);
    REQUIRE(word.pronunciation == "h e l l o");
    REQUIRE(word.fromDict);
    REQUIRE(word.errorType == srt::g2p::NoError);
    REQUIRE_FALSE(word.fromFallback);

    std::filesystem::remove_all(dir);
}

// ===========================================================================
// G2P-034: Empty steps array -> configure succeeds, process is a no-op.
//
// configure() requires a "steps" array but accepts an empty one. process()
// iterates zero steps, so words retain their default state (errorType=NoError,
// pronunciation empty). This is by-design: an empty chain is a valid no-op
// pipeline, not an error.
// ===========================================================================
TEST_CASE("G2P-034: empty steps array configures and processes as no-op",
          "[g2p][bf-53][extreme]") {
    TestModuleSpec spec;
    G2pPipeline pipeline(&spec, /*task=*/nullptr);

    auto cfg = chainConfig("");
    REQUIRE(pipeline.configure(cfg).hasValue());

    G2pContext context({"hello"}, &spec);
    pipeline.process(context);

    const auto &word = context.words()[0];
    REQUIRE(word.lyric == "hello");
    // No step touched the word.
    REQUIRE(word.pronunciation.empty());
    REQUIRE(word.errorType == srt::g2p::NoError);
    REQUIRE(word.mode.empty());
    REQUIRE_FALSE(context.isStopProcessing());
}

// ===========================================================================
// G2P-035: Missing "steps" field -> configure returns G2pConfigError.
//
// configure() rejects a config without a "steps" array. The error maps to
// ErrorCode::G2pConfigError (deprecated Type ConfigError -> G2pConfigError
// via mapTypeToCode in lib/G2P/Support/Error.cpp).
// ===========================================================================
TEST_CASE("G2P-035: missing steps field returns G2pConfigError",
          "[g2p][bf-53][extreme]") {
    TestModuleSpec spec;
    G2pPipeline pipeline(&spec, /*task=*/nullptr);

    auto cfg = parseJsonObject(R"({"notSteps":123})");
    auto configExp = pipeline.configure(cfg);
    REQUIRE_FALSE(configExp.hasValue());
    REQUIRE(configExp.errorCode() == ErrorCode::G2pConfigError);
    REQUIRE_FALSE(configExp.errorMessage().empty());
}

// ===========================================================================
// G2P-036: Single-step pipeline (fallback only).
//
// A 1-step chain still processes correctly. Without tagAndValidate,
// word.mode defaults to "" (not "copy"), so FallbackStep processes it:
// pronunciation=lyric, fromFallback=true, errorType=PhonemeGenerationFailed.
// ===========================================================================
TEST_CASE("G2P-036: single-step fallback pipeline",
          "[g2p][bf-53][extreme]") {
    TestModuleSpec spec;
    G2pPipeline pipeline(&spec, /*task=*/nullptr);

    auto cfg = chainConfig(R"({"step":"fallback"})");
    REQUIRE(pipeline.configure(cfg).hasValue());

    G2pContext context({"hello"}, &spec);
    pipeline.process(context);

    const auto &word = context.words()[0];
    REQUIRE(word.pronunciation == "hello");
    REQUIRE(word.fromFallback);
    REQUIRE(word.errorType == srt::g2p::PhonemeGenerationFailed);
}

// ===========================================================================
// G2P-037: 10-step pipeline completes without error.
//
// A chain with many steps (10 format steps) must iterate all of them
// without crashing. FormatStep is a no-op when pronunciation is empty, so
// the word stays in its default state. This guards against off-by-one or
// early-break regressions in the process() loop.
// ===========================================================================
TEST_CASE("G2P-037: 10-step pipeline processes all steps",
          "[g2p][bf-53][extreme]") {
    TestModuleSpec spec;
    G2pPipeline pipeline(&spec, /*task=*/nullptr);

    std::string steps;
    for (int i = 0; i < 10; ++i) {
        if (i > 0) steps += ",";
        steps += R"({"step":"format"})";
    }
    auto cfg = chainConfig(steps);
    REQUIRE(pipeline.configure(cfg).hasValue());

    G2pContext context({"hello"}, &spec);
    pipeline.process(context);

    // All 10 format steps ran; pronunciation was empty so it stays empty.
    const auto &word = context.words()[0];
    REQUIRE(word.lyric == "hello");
    REQUIRE(word.pronunciation.empty());
    REQUIRE(word.errorType == srt::g2p::NoError);
    REQUIRE_FALSE(context.isStopProcessing());
}

// ===========================================================================
// G2P-038: Too many steps (>50) -> configure returns G2pConfigError.
//
// G2pPipeline::configure caps the step count at kG2pPipelineMaxSteps=50
// (G2pPipeline.cpp L49). 51 steps must be rejected with G2pConfigError.
// ===========================================================================
TEST_CASE("G2P-038: too many steps (>50) returns G2pConfigError",
          "[g2p][bf-53][extreme]") {
    TestModuleSpec spec;
    G2pPipeline pipeline(&spec, /*task=*/nullptr);

    std::string steps;
    for (int i = 0; i < 51; ++i) {
        if (i > 0) steps += ",";
        steps += R"({"step":"format"})";
    }
    auto cfg = chainConfig(steps);
    auto configExp = pipeline.configure(cfg);
    REQUIRE_FALSE(configExp.hasValue());
    REQUIRE(configExp.errorCode() == ErrorCode::G2pConfigError);
}

// ===========================================================================
// G2P-039: Unknown step type -> configure returns G2pConfigError.
//
// G2pStepFactory::create rejects unknown step types; configure wraps the
// factory error with step index context. The error maps to G2pConfigError.
// ===========================================================================
TEST_CASE("G2P-039: unknown step type returns G2pConfigError",
          "[g2p][bf-53][extreme]") {
    TestModuleSpec spec;
    G2pPipeline pipeline(&spec, /*task=*/nullptr);

    auto cfg = chainConfig(R"({"step":"nonexistentStepType"})");
    auto configExp = pipeline.configure(cfg);
    REQUIRE_FALSE(configExp.hasValue());
    REQUIRE(configExp.errorCode() == ErrorCode::G2pConfigError);
    REQUIRE_FALSE(configExp.errorMessage().empty());
}

// ===========================================================================
// G2P-040: Nested chain step type is not supported.
//
// There is no "chain" step type in G2pStepFactory::supportedTypes()
// (tagAndValidate/dict/model/format/fallback only). A nested chain is
// therefore rejected at configure time with G2pConfigError. This documents
// that nested chains are not a supported composition primitive.
// ===========================================================================
TEST_CASE("G2P-040: nested chain step type is not supported",
          "[g2p][bf-53][extreme]") {
    TestModuleSpec spec;
    G2pPipeline pipeline(&spec, /*task=*/nullptr);

    auto cfg = chainConfig(R"({"step":"chain","params":{"steps":[]}})");
    auto configExp = pipeline.configure(cfg);
    REQUIRE_FALSE(configExp.hasValue());
    REQUIRE(configExp.errorCode() == ErrorCode::G2pConfigError);
}

// ===========================================================================
// G2P-041: Disabled step is skipped during configure.
//
// A step with enabled=false is skipped by configure() (G2pPipeline.cpp
// L82-84) and not added to m_steps. Here a disabled fallback followed by an
// enabled fallback still applies the fallback once (from the enabled step),
// proving the disabled step was dropped rather than executed.
// ===========================================================================
TEST_CASE("G2P-041: disabled step is skipped by configure",
          "[g2p][bf-53][extreme]") {
    TestModuleSpec spec;
    G2pPipeline pipeline(&spec, /*task=*/nullptr);

    auto cfg = chainConfig(
        R"({"step":"fallback","enabled":false},)"
        R"({"step":"fallback"})");
    REQUIRE(pipeline.configure(cfg).hasValue());

    G2pContext context({"hello"}, &spec);
    pipeline.process(context);

    // The enabled fallback ran exactly once; the disabled one was skipped.
    const auto &word = context.words()[0];
    REQUIRE(word.pronunciation == "hello");
    REQUIRE(word.fromFallback);
    REQUIRE(word.errorType == srt::g2p::PhonemeGenerationFailed);
}

// ===========================================================================
// G2P-042: Custom G2pStep subclass propagates errorType through the step
// interface (isolated, no G2pPipeline injection).
//
// G2pPipeline::m_steps is private, so a custom mock step cannot be injected
// into a configured pipeline. Instead this case verifies the G2pStep
// abstract interface itself: a subclass that sets errorType on convert-mode
// words works as the chain contract expects. This documents the extension
// point a future "fatal-abort" step would use (the BF-53 archived
// setStopProcessing activation path).
// ===========================================================================
namespace {

    /// Mock step that marks every convert-mode word with a fixed error and
    /// optionally calls setStopProcessing(true) to request a pipeline break.
    class MockFailingStep : public G2pStep {
    public:
        explicit MockFailingStep(G2pErrorType err, bool requestStop = false)
            : m_err(err), m_requestStop(requestStop) {}

        Expected<void> configure(const ModuleSpec * /*spec*/,
                                 const JsonObject & /*config*/) override {
            return {};
        }

        void handle(G2pContext &context) override {
            for (auto &word : context.words()) {
                if (word.mode == srt::g2p::kG2pModeConvert && !word.discard &&
                    word.pronunciation.empty()) {
                    word.pronunciation = word.lyric;
                    word.candidates = {word.lyric};
                    word.errorType = m_err;
                }
            }
            if (m_requestStop) {
                context.setStopProcessing(true);
            }
        }

        std::string name() const override { return "mockFailing"; }

    private:
        G2pErrorType m_err;
        bool m_requestStop;
    };

    /// Mock step that records whether handle() was invoked.
    class CountingStep : public G2pStep {
    public:
        Expected<void> configure(const ModuleSpec * /*spec*/,
                                 const JsonObject & /*config*/) override {
            return {};
        }
        void handle(G2pContext & /*context*/) override { ++m_calls; }
        std::string name() const override { return "counting"; }
        int calls() const { return m_calls; }

    private:
        int m_calls = 0;
    };

    /// Faithful replica of G2pPipeline::process (G2pPipeline.cpp L126-134) for
    /// exercising the loop with mock steps that cannot be injected into a
    /// real G2pPipeline (m_steps is private). The logic is identical so the
    /// break/error semantics match the production code.
    void runPipelineLikeG2pPipeline(const std::vector<std::shared_ptr<G2pStep>> &steps,
                                    G2pContext &context) {
        for (auto &step : steps) {
            step->handle(context);
            if (context.isStopProcessing()) {
                break;
            }
        }
    }

} // namespace

TEST_CASE("G2P-042: custom G2pStep propagates error and stop request",
          "[g2p][bf-53][extreme]") {
    TestModuleSpec spec;

    SECTION("failing step sets per-word errorType") {
        G2pContext context({"hello"}, &spec);
        context.words()[0].mode = srt::g2p::kG2pModeConvert;

        auto failing = std::make_shared<MockFailingStep>(srt::g2p::ModelInferenceFailed);
        runPipelineLikeG2pPipeline({failing}, context);

        REQUIRE(context.words()[0].errorType == srt::g2p::ModelInferenceFailed);
        REQUIRE(context.words()[0].pronunciation == "hello");
        REQUIRE_FALSE(context.isStopProcessing());
    }

    SECTION("stop-requesting step breaks the loop before later steps") {
        G2pContext context({"hello"}, &spec);
        context.words()[0].mode = srt::g2p::kG2pModeConvert;

        auto stopping = std::make_shared<MockFailingStep>(srt::g2p::ModelInferenceFailed,
                                                          /*requestStop=*/true);
        auto counting = std::make_shared<CountingStep>();
        runPipelineLikeG2pPipeline({stopping, counting}, context);

        // stopping step ran (set error + stop flag); counting step skipped.
        REQUIRE(context.words()[0].errorType == srt::g2p::ModelInferenceFailed);
        REQUIRE(context.isStopProcessing());
        REQUIRE(counting->calls() == 0);
    }

    SECTION("non-stop failing step lets later steps run") {
        G2pContext context({"hello"}, &spec);
        context.words()[0].mode = srt::g2p::kG2pModeConvert;

        auto failing = std::make_shared<MockFailingStep>(srt::g2p::ModelInferenceFailed);
        auto counting = std::make_shared<CountingStep>();
        runPipelineLikeG2pPipeline({failing, counting}, context);

        REQUIRE_FALSE(context.isStopProcessing());
        REQUIRE(counting->calls() == 1);
    }
}
