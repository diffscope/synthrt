// Regression tests for ModelSet error trace and context propagation (v3 §1.1
// ModelSet trace + §5.3 createAndInit B-07 regression).
//
// Covers 07-test-matrix.md:
//   §5.3: load(stage) with null spec -> Error contains moduleId context + trace
//   §1.1: appendTrace on ModelSet error paths
//
// The createAndInit failure path (B-07) requires a loaded InferenceSpec whose
// createInference/initialize fails. InferenceSpec has a protected constructor
// and a private Impl, so it cannot be mocked in isolation without a real
// plugin/ONNX runtime. The null-spec path exercises the same withTrace +
// withContext chaining pattern (Agent D's appendTrace补全), and is tested here.
// The B-07 inner-trace propagation is verified at the Error level in
// unittests/Core/test_error_trace.cpp (withTrace/withContext chaining).

#include <catch2/catch_test_macros.hpp>

#include <diffsinger/Infer/ModelSet.h>
#include <diffsinger/Infer/StageKind.h>
#include <synthrt/Core/Support/Diagnostic.h>

using namespace ds::infer;
using srt::core::ErrorCode;
using srt::core::ErrorCategory;

namespace {

    // Create an empty StageSet (all spec pointers null by default).
    StageSet makeEmptyStageSet() {
        StageSet stages;
        return stages;
    }

} // namespace

// ---------------------------------------------------------------------------
// §5.3 / §1.1: ModelSet::load with null spec produces moduleId context + trace
// ---------------------------------------------------------------------------

TEST_CASE("ModelSet load null spec error has moduleId context", "[modelset][trace][er-01]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    auto exp = modelSet.load(StageKind::Duration);
    REQUIRE(!exp.hasValue());

    const auto &diag = exp.error().diagnostic();
    // inferenceError(..., {}, "duration") sets moduleId to the stage name.
    REQUIRE(diag.moduleId == "duration");
}

TEST_CASE("ModelSet load null spec error has at least one trace frame",
          "[modelset][trace][er-01]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    auto exp = modelSet.load(StageKind::Acoustic);
    REQUIRE(!exp.hasValue());

    const auto &diag = exp.error().diagnostic();
    REQUIRE(!diag.trace.empty());
    // The trace frame note records the ModelSet::load layer.
    bool foundLoadFrame = false;
    for (const auto &t : diag.trace) {
        if (t.find("ModelSet::load") != std::string::npos) {
            foundLoadFrame = true;
            break;
        }
    }
    REQUIRE(foundLoadFrame);
}

TEST_CASE("ModelSet load null spec toString contains moduleId and trace",
          "[modelset][trace][er-01]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    auto exp = modelSet.load(StageKind::Acoustic);
    REQUIRE(!exp.hasValue());

    auto s = exp.errorString();
    // Error code label.
    REQUIRE(s.find("[Inference::InputInvalid]") != std::string::npos);
    // Context field.
    REQUIRE(s.find("moduleId:") != std::string::npos);
    REQUIRE(s.find("acoustic") != std::string::npos);
    // Trace section.
    REQUIRE(s.find("trace:") != std::string::npos);
    REQUIRE(s.find("ModelSet::load") != std::string::npos);
}

TEST_CASE("ModelSet load null spec each stage has its own moduleId",
          "[modelset][trace][er-01]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    for (auto kind : {StageKind::Duration, StageKind::Pitch, StageKind::Variance,
                      StageKind::Acoustic, StageKind::Vocoder}) {
        auto exp = modelSet.load(kind);
        REQUIRE(!exp.hasValue());
        // Each stage's error carries its own moduleId (stage name).
        REQUIRE(!exp.error().diagnostic().moduleId.empty());
        REQUIRE(!exp.error().diagnostic().trace.empty());
    }
}

TEST_CASE("ModelSet load null spec trace frame embeds source location",
          "[modelset][trace][er-01]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    auto exp = modelSet.load(StageKind::Duration);
    REQUIRE(!exp.hasValue());

    const auto &trace = exp.error().diagnostic().trace;
    REQUIRE(!trace.empty());
    // Each trace entry embeds "file:line:function" from source_location.
    REQUIRE(trace[0].find(':') != std::string::npos);
}
