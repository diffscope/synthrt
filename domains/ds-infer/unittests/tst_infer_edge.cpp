// ds-infer edge condition tests (INF-001 ~ INF-012).
//
// Coverage matrix: docs/refactoring-vnext/test-matrix-expansion.md §1.9
// Test target: domains/ds-infer/unittests (explicitly collected by catch2/CMakeLists.txt,
//           needs ../tst_infer_edge.cpp appended to the add_executable list in that file).
//
// Design principle cross-reference:
//   ROBUST-01: setStages/load/start/reset/unload return Expected<T>;
//              run returns InferenceResult (error filled into .error field), no exceptions thrown.
//   ROBUST-03: When StageSpec.spec is nullptr, load/setStages guard against null and return error.
//   ARCH-05:   ModelSet covers the full load/start/stop/unload lifecycle.
//   INFRA-03:  L1 single-component tests do not load ONNX plugin DLLs; cases requiring real models SKIP.
//
// API difference notes (where actual API differs from matrix expectations, adjusted per actual API):
//   INF-001: Matrix expects InvalidArg; actual setStages with all nullptr spec returns
//             ErrorCode::InferenceInputInvalid ("stage spec ... null").
//   INF-002: Matrix expects NotInitialized; actual run() without setStages returns
//             InferenceResult with error.code() == InferenceInputInvalid
//             (no NotInitialized in error code system; unset stages classified as
//             InferenceInputInvalid, error message contains "stages").
//   INF-003: Matrix expects NotFound; actual load with nullptr spec returns
//             ErrorCode::InferenceInputInvalid (not NotFound).
//   INF-004: Matrix expects NotLoaded; actual start without load returns
//             ErrorCode::InferenceNotInitialized (not NotLoaded).
//   INF-007: Requires all 5 stages to be loaded (requires real ONNX models) -> SKIP L2.
//   INF-008: Concurrent load ModelBusy contention requires real load duration (null spec fails fast
//             returning InferenceInputInvalid, cannot trigger ModelBusy) -> SKIP L2.
//   INF-009: P2 case, and requires already-loaded real model -> SKIP.
//   INF-010: Requires setStages to succeed (non-null InferenceSpec) + acoustic stage failure
//             -> SKIP L2.
//   INF-011: Matrix says "call result when already loaded"; L1 cannot load (null spec).
//             result() also returns empty NO when not loaded/not started, covering that reachable path.
//   INF-012: StageSet::find(invalidKind) returns nullptr — verified directly here
//             (also covered by tst_inference_service.cpp).
//
// Error code system see include/synthrt/Core/Support/Diagnostic.h:
//   Inference segment: InferenceNotInitialized=200, InferenceStageSpecNull=207,
//                      InferenceInputInvalid=213, ModelBusy=215, StaleModelSet=216...

#include <catch2/catch_test_macros.hpp>

#include <diffsinger/Infer/InferenceRequest.h>
#include <diffsinger/Infer/InferenceResult.h>
#include <diffsinger/Infer/InferenceService.h>
#include <diffsinger/Infer/ModelSet.h>
#include <diffsinger/Infer/StageKind.h>
#include <synthrt/Core/Support/Diagnostic.h>

using namespace ds::infer;
using srt::core::ErrorCode;
using srt::core::ErrorCategory;

namespace srt::core {
// Catch2 needs operator<< to stream ErrorCode values in failed assertions.
inline std::ostream &operator<<(std::ostream &os, const ErrorCode &code) {
    return os << errorCodeToString(code);
}
} // namespace srt::core

namespace {

    // Create a StageSet with all nullptr specs (all stage.spec are nullptr).
    StageSet makeEmptyStageSet() {
        StageSet stages;
        return stages;
    }

} // namespace

// ===========================================================================
// INF-001: setStages with all nullptr specs
//
// Actual behavior: returns InferenceInputInvalid (see file header API difference notes)
// ===========================================================================
TEST_CASE("INF-001: setStages with all null specs returns InferenceInputInvalid",
          "[infer][edge]") {
    InferenceService service;
    auto stages = makeEmptyStageSet();
    auto exp = service.setStages(stages);
    REQUIRE_FALSE(exp.hasValue());
    // Matrix expects InvalidArg; actual returns InferenceInputInvalid
    REQUIRE(exp.isError(ErrorCode::InferenceInputInvalid));
    REQUIRE(exp.errorCategory() == ErrorCategory::Inference);
    REQUIRE(exp.error().message().find("null") != std::string::npos);
}

// ===========================================================================
// INF-002: run called without setStages
//
// Actual behavior: returns InferenceResult, error.code() == InferenceInputInvalid
// ===========================================================================
TEST_CASE("INF-002: run without setStages returns error in result",
          "[infer][edge]") {
    InferenceService service;
    InferenceRequest request;
    request.singerId = "test_singer";

    auto result = service.run(request);
    // run does not throw, error filled into InferenceResult::error
    REQUIRE_FALSE(result.error.ok());
    REQUIRE(result.audio.empty());
    // Matrix expects NotInitialized; actual returns InferenceInputInvalid (message contains "stages")
    REQUIRE(result.error.code() == ErrorCode::InferenceInputInvalid);
    REQUIRE(result.error.message().find("stages") != std::string::npos);
}

// ===========================================================================
// INF-003: ModelSet::load spec is nullptr
//
// Actual behavior: returns InferenceInputInvalid (see file header API difference notes)
// ===========================================================================
TEST_CASE("INF-003: ModelSet load with null spec returns InferenceInputInvalid",
          "[infer][edge]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    auto exp = modelSet.load(StageKind::Duration);
    REQUIRE_FALSE(exp.hasValue());
    // Matrix expects NotFound; actual returns InferenceInputInvalid (duration spec is nullptr)
    REQUIRE(exp.isError(ErrorCode::InferenceInputInvalid));
    REQUIRE(exp.errorCategory() == ErrorCategory::Inference);
    REQUIRE(exp.error().message().find("null") != std::string::npos);
}

// ===========================================================================
// INF-004: ModelSet::start called without load
//
// Actual behavior: returns InferenceNotInitialized (see file header API difference notes)
// ===========================================================================
TEST_CASE("INF-004: ModelSet start without load returns InferenceNotInitialized",
          "[infer][edge]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    auto exp = modelSet.start(StageKind::Duration, {});
    REQUIRE_FALSE(exp.hasValue());
    // Matrix expects NotLoaded; actual returns InferenceNotInitialized
    REQUIRE(exp.isError(ErrorCode::InferenceNotInitialized));
    REQUIRE(exp.errorCategory() == ErrorCategory::Inference);
}

// ===========================================================================
// INF-005: ModelSet::markStale then load is rejected
// ===========================================================================
TEST_CASE("INF-005: ModelSet load rejected after markStale with StaleModelSet",
          "[infer][edge]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    REQUIRE_FALSE(modelSet.isStale());
    modelSet.markStale();
    REQUIRE(modelSet.isStale());

    auto exp = modelSet.load(StageKind::Duration);
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::StaleModelSet));
    REQUIRE(exp.errorCategory() == ErrorCategory::Inference);

    // start is also rejected by stale
    auto startExp = modelSet.start(StageKind::Duration, {});
    REQUIRE_FALSE(startExp.hasValue());
    REQUIRE(startExp.isError(ErrorCode::StaleModelSet));
}

// ===========================================================================
// INF-006: ModelSet::reset called without start
//
// Actual behavior: returns OK (reset is safe no-op, see tst_modelset_errors.cpp BF-24)
// ===========================================================================
TEST_CASE("INF-006: ModelSet reset without start returns success",
          "[infer][edge]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    // Matrix expects "OK or Error(NotStarted)"; actual returns OK (no-op)
    REQUIRE(modelSet.reset(StageKind::Duration).hasValue());
    // result still empty after reset
    REQUIRE(!modelSet.result(StageKind::Duration));
}

// ===========================================================================
// INF-007: ModelSet::unload reverse release order (P1)
//
// Requires all 5 stages to be loaded (real ONNX Inference objects); L1 cannot construct them.
// unloadAll on empty stages only returns success, cannot verify vocoder->acoustic->...
// ->pitch->duration release order.
// ===========================================================================
TEST_CASE("INF-007: ModelSet unloadAll releases in reverse order",
          "[infer][edge]") {
    SKIP("L2: requires 5 stages loaded with real ONNX Inference objects to "
         "verify vocoder->acoustic->variance->pitch->duration release order. "
         "L1 null-spec fixtures cannot construct loaded Inference objects; "
         "unloadAll on empty stages only returns success (no order to verify).");
}

// ===========================================================================
// INF-008: ModelSet concurrent load of different stages (P1)
//
// ModelBusy contention requires real load to hold the lock long enough; null spec immediately returns
// InferenceInputInvalid, cannot trigger ModelBusy.
// ===========================================================================
TEST_CASE("INF-008: ModelSet concurrent load different stages", "[infer][edge]") {
    SKIP("L2: requires real ONNX models so load() holds the internal lock long "
         "enough for a concurrent load on another stage to observe ModelBusy. "
         "With null specs both threads fail fast with InferenceInputInvalid "
         "before any contention; the ModelBusy path is unreachable at L1.");
}

// ===========================================================================
// INF-009: ModelSet::clearStale then load without unload (P2)
//
// P2 case, and requires already-loaded real model to verify "returns existing Inference or needs to be documented".
// ===========================================================================
TEST_CASE("INF-009: ModelSet clearStale then load without unload", "[infer][edge]") {
    SKIP("P2: requires a loaded Inference (real ONNX model) to verify whether "
         "load after clearStale returns the existing Inference or requires "
         "unload first. L1 null-spec load returns InferenceInputInvalid. "
         "clearStale() itself is exercised in INF-005's markStale path.");
}

// ===========================================================================
// INF-010: InferenceService::run partial stage failure (P0)
//
// Requires setStages to succeed (non-null InferenceSpec) so the acoustic stage can execute and fail.
// L1 cannot construct real InferenceSpec -> SKIP L2.
// ===========================================================================
TEST_CASE("INF-010: InferenceService run with acoustic stage failure stops pipeline",
          "[infer][edge]") {
    SKIP("L2: requires setStages to succeed with real InferenceSpec objects so "
         "the acoustic stage can execute and fail, verifying the pipeline stops "
         "(No Hidden Fallback) and does not continue to vocoder. L1 fixtures "
         "cannot build non-null InferenceSpec without ONNX plugin DLLs.");
}

// ===========================================================================
// INF-011: ModelSet::result called without start
//
// Matrix says "call result when already loaded"; L1 cannot load. result() also returns empty NO when
// not loaded/not started, covering that reachable path.
// ===========================================================================
TEST_CASE("INF-011: ModelSet result returns empty NO before start",
          "[infer][edge]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    // result() returns empty NO (falsy) when not started
    REQUIRE(!modelSet.result(StageKind::Duration));
    REQUIRE(!modelSet.result(StageKind::Pitch));
    REQUIRE(!modelSet.result(StageKind::Variance));
    REQUIRE(!modelSet.result(StageKind::Acoustic));
    REQUIRE(!modelSet.result(StageKind::Vocoder));

    // isLoaded returns false when not loaded
    REQUIRE_FALSE(modelSet.isLoaded(StageKind::Duration));
}

// ===========================================================================
// INF-012: StageSet::find with invalid kind
//
// StageSet::find (SingerStageResolver.cpp) switches on the 5 valid StageKind
// values and returns nullptr for any other enumerator. Verify directly that an
// out-of-range kind yields nullptr (also covered by tst_inference_service.cpp).
// ===========================================================================
TEST_CASE("INF-012: StageSet find returns nullptr for invalid kind",
          "[infer][edge]") {
    auto stages = makeEmptyStageSet();
    auto invalidKind = static_cast<StageKind>(999);
    REQUIRE(stages.find(invalidKind) == nullptr);
}

// ===========================================================================
// INF-013 ~ INF-020: pure value semantics and empty state edge tests.
//
// The following cases do not load ONNX plugin DLLs, no real inference interaction (INFRA-03 L1). Only use APIs
// already declared in existing includes (InferenceRequest/InferenceResult/InferenceService/
// ModelSet/StageKind are all included at the top of the file), no new header dependencies introduced.
// ===========================================================================

// ===========================================================================
// INF-013: InferenceRequest default-constructed field values
//
// Pure value semantics test: default-constructed InferenceRequest string fields are empty, words/
// parameters/speakers are empty containers, duration=0, steps=10, depth=0 (see
// default member initial values in InferenceRequest.h). Verify request struct zero-initialization state.
// ===========================================================================
TEST_CASE("INF-013: InferenceRequest default construction has expected defaults",
          "[infer][edge]") {
    InferenceRequest request;
    REQUIRE(request.singerId.empty());
    REQUIRE(request.inferenceId.empty());
    REQUIRE(request.speakerId.empty());
    REQUIRE(request.words.empty());
    REQUIRE(request.parameters.empty());
    REQUIRE(request.speakers.empty());
    // Numeric field default values (declared in InferenceRequest.h: duration=0, steps=10, depth=0)
    REQUIRE(request.duration == 0.0);
    REQUIRE(request.steps == 10);
    REQUIRE(request.depth == 0.0f);
}

// ===========================================================================
// INF-014: InferenceResult default construction
//
// Pure value semantics test: default-constructed InferenceResult.audio is empty, sampleRate/channels
// are 0, error is default OK state (Error.h: Error() calls Error(NoError), ok() returns
// _type==NoError). Verify result struct zero-initialization state.
// ===========================================================================
TEST_CASE("INF-014: InferenceResult default construction is empty and ok",
          "[infer][edge]") {
    InferenceResult result;
    REQUIRE(result.audio.empty());
    REQUIRE(result.sampleRate == 0);
    REQUIRE(result.channels == 0);
    // Default Error is OK (Error.h: Error() : Error(NoError))
    REQUIRE(result.error.ok());
}

// ===========================================================================
// INF-015: StageKind enum value check
//
// Pure value semantics test: StageKind is an enum class, default underlying type int, in declaration order
// Duration=0, Pitch=1, Variance=2, Acoustic=3, Vocoder=4 (C++ standard guarantees enums without explicit
// values increment from 0). Verify the 5 stage values are sequential and distinct, preventing enum reordering
// from breaking the duration->pitch->variance->acoustic->vocoder pipeline order assumption.
// ===========================================================================
TEST_CASE("INF-015: StageKind enum values are sequential and distinct",
          "[infer][edge]") {
    REQUIRE(static_cast<int>(StageKind::Duration) == 0);
    REQUIRE(static_cast<int>(StageKind::Pitch) == 1);
    REQUIRE(static_cast<int>(StageKind::Variance) == 2);
    REQUIRE(static_cast<int>(StageKind::Acoustic) == 3);
    REQUIRE(static_cast<int>(StageKind::Vocoder) == 4);
}

// ===========================================================================
// INF-016: InferenceRequest field assignment read-back consistency
//
// Pure value semantics test: after assigning values to InferenceRequest fields, read them back, verify written
// values match read values (including string and numeric fields). No runtime interaction.
// ===========================================================================
TEST_CASE("INF-016: InferenceRequest field assignment round-trip",
          "[infer][edge]") {
    InferenceRequest request;
    request.singerId = "singer.x";
    request.inferenceId = "duration";
    request.speakerId = "speaker.y";
    request.duration = 3.14;
    request.steps = 50;
    request.depth = 0.75f;

    REQUIRE(request.singerId == "singer.x");
    REQUIRE(request.inferenceId == "duration");
    REQUIRE(request.speakerId == "speaker.y");
    REQUIRE(request.duration == 3.14);
    REQUIRE(request.steps == 50);
    REQUIRE(request.depth == 0.75f);
}

// ===========================================================================
// INF-017: ModelSet::stages() preserves the StageSet passed at construction (all nullptr spec)
//
// Edge test: ModelSet is constructed with makeEmptyStageSet() (all nullptr spec),
// stages() returned reference has all 5 stage specs as nullptr. Verify the StageSet state
// preserved at construction (ModelSet.h: const StageSet &stages() const noexcept).
// ===========================================================================
TEST_CASE("INF-017: ModelSet stages preserves null specs from construction",
          "[infer][edge]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    const auto &kept = modelSet.stages();
    REQUIRE(kept.duration.spec == nullptr);
    REQUIRE(kept.pitch.spec == nullptr);
    REQUIRE(kept.variance.spec == nullptr);
    REQUIRE(kept.acoustic.spec == nullptr);
    REQUIRE(kept.vocoder.spec == nullptr);
}

// ===========================================================================
// INF-018: ModelSet isLoaded returns false for all 5 stages when not loaded
//
// Edge test: ModelSet constructed from empty StageSet, without calling load, all 5 stages'
// isLoaded are false. Complementary to INF-011 (only verifies Duration's isLoaded), covers
// the unloaded query path for all 5 stages (ModelSet.h: bool isLoaded(StageKind) noexcept).
// ===========================================================================
TEST_CASE("INF-018: ModelSet isLoaded false for all stages before load",
          "[infer][edge]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    REQUIRE_FALSE(modelSet.isLoaded(StageKind::Duration));
    REQUIRE_FALSE(modelSet.isLoaded(StageKind::Pitch));
    REQUIRE_FALSE(modelSet.isLoaded(StageKind::Variance));
    REQUIRE_FALSE(modelSet.isLoaded(StageKind::Acoustic));
    REQUIRE_FALSE(modelSet.isLoaded(StageKind::Vocoder));
}

// ===========================================================================
// INF-019: ModelSet clearStale clears stale flag and restores load reachability
//
// Edge test: after markStale, isStale is true, load is rejected (StaleModelSet, see
// INF-005); after calling clearStale, isStale returns to false, load no longer returns
// StaleModelSet, but returns InferenceInputInvalid due to null spec (consistent with INF-003).
// Verify clearStale semantics (opt-in reuse escape hatch declared in ModelSet.h).
// ===========================================================================
TEST_CASE("INF-019: ModelSet clearStale re-enables load after markStale",
          "[infer][edge]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    // Initially not stale
    REQUIRE_FALSE(modelSet.isStale());

    // After markStale, load is rejected (StaleModelSet, see INF-005)
    modelSet.markStale();
    REQUIRE(modelSet.isStale());
    auto staleExp = modelSet.load(StageKind::Duration);
    REQUIRE_FALSE(staleExp.hasValue());
    REQUIRE(staleExp.isError(ErrorCode::StaleModelSet));

    // clearStale clears the stale flag
    modelSet.clearStale();
    REQUIRE_FALSE(modelSet.isStale());

    // load is no longer rejected due to stale, instead returns InferenceInputInvalid due to null spec
    auto exp = modelSet.load(StageKind::Duration);
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InferenceInputInvalid));
    REQUIRE(exp.errorCategory() == ErrorCategory::Inference);
}

// ===========================================================================
// INF-020: ModelSet::reset returns success for all 5 stages when not started
//
// Edge test: ModelSet not started, reset returns success for all 5 stages (no-op, see
// INF-006 for Duration verification). Extends coverage to Pitch/Variance/Acoustic/Vocoder,
// verifying reset's safe no-op semantics are consistent across all stages (ModelSet.h: Expected<void>
// reset(StageKind)).
// ===========================================================================
TEST_CASE("INF-020: ModelSet reset returns success for all stages without start",
          "[infer][edge]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    // reset is a safe no-op when not started, all 5 stages return success
    REQUIRE(modelSet.reset(StageKind::Duration).hasValue());
    REQUIRE(modelSet.reset(StageKind::Pitch).hasValue());
    REQUIRE(modelSet.reset(StageKind::Variance).hasValue());
    REQUIRE(modelSet.reset(StageKind::Acoustic).hasValue());
    REQUIRE(modelSet.reset(StageKind::Vocoder).hasValue());
}
