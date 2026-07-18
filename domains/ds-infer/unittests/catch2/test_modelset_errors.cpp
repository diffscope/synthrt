// Unit tests for ds::infer::ModelSet error paths and BF-24 regression.
//
// Covers:
//   a. load() error codes — null spec returns ErrorCode::InferenceInputInvalid
//   b. BF-24: stop() on non-Running model returns success (not error)
//   c. unload() on non-loaded model returns success (no-op)
//
// Error codes migrated from Error::SessionError to specific
// ErrorCode::Inference* values (see 01-error-system.md).
//
// Note: load() with a truly invalid StageKind cannot be tested here because
// ModelSet::Impl::slot() calls std::abort() for out-of-range enum values.
// The createForKind() fallback returns InferenceInputInvalid, but it is
// unreachable through the public load() API for valid StageKind values.

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
// a. ModelSet load() error codes
// ---------------------------------------------------------------------------

TEST_CASE("ModelSet load null spec returns InferenceInputInvalid", "[modelset][error]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    auto exp = modelSet.load(StageKind::Duration);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InferenceInputInvalid));
    REQUIRE(exp.errorCategory() == ErrorCategory::Inference);
}

TEST_CASE("ModelSet load null spec all stages return InferenceInputInvalid", "[modelset][error]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    for (auto kind : {StageKind::Duration, StageKind::Pitch, StageKind::Variance,
                      StageKind::Acoustic, StageKind::Vocoder}) {
        auto exp = modelSet.load(kind);
        REQUIRE(!exp.hasValue());
        REQUIRE(exp.errorCode() == ErrorCode::InferenceInputInvalid);
    }
}

TEST_CASE("ModelSet load null spec error message contains null", "[modelset][error]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    auto exp = modelSet.load(StageKind::Acoustic);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.error().message().find("null") != std::string::npos);
}

// ---------------------------------------------------------------------------
// b. BF-24 regression: stop() on already-stopped / non-running model
//    stop() must return success when the model is not Running (Idle, Stopped,
//    Terminated, Failed) or not loaded at all.
// ---------------------------------------------------------------------------

TEST_CASE("BF-24 stop on never-loaded model returns success", "[modelset][bf-24]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    // A model that was never started (slot is null) must not report an error.
    REQUIRE(modelSet.stop(StageKind::Duration).hasValue());
    REQUIRE(modelSet.stop(StageKind::Vocoder).hasValue());
}

TEST_CASE("BF-24 stop on never-loaded model all stages returns success", "[modelset][bf-24]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    for (auto kind : {StageKind::Duration, StageKind::Pitch, StageKind::Variance,
                      StageKind::Acoustic, StageKind::Vocoder}) {
        REQUIRE(modelSet.stop(kind).hasValue());
    }
}

TEST_CASE("BF-24 stop is idempotent on non-loaded model", "[modelset][bf-24]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    REQUIRE(modelSet.stop(StageKind::Duration).hasValue());
    // Second stop on the same non-loaded stage must also succeed.
    REQUIRE(modelSet.stop(StageKind::Duration).hasValue());
}

// TODO: requires ONNX runtime — the following BF-24 guard cannot be tested
// without a loaded Inference object:
//   stop() on a model that is loaded but in Idle/Terminated/Failed state
//   (slot->state() != ITask::Running) must return success.
// ModelSet::Impl::stop() checks: if (slot->state() != Running) return success;
// To exercise this, a loaded Inference (via createInference + initialize) is
// required, which needs ONNX runtime and a valid model file.

TEST_CASE("ModelSet stale set rejects load and start", "[modelset][lifecycle]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    REQUIRE(!modelSet.isStale());
    modelSet.markStale();
    REQUIRE(modelSet.isStale());

    auto loadExp = modelSet.load(StageKind::Duration);
    REQUIRE(!loadExp.hasValue());
    REQUIRE(loadExp.isError(ErrorCode::StaleModelSet));

    auto startExp = modelSet.start(StageKind::Duration, {});
    REQUIRE(!startExp.hasValue());
    REQUIRE(startExp.isError(ErrorCode::StaleModelSet));
}

TEST_CASE("ModelSet result is empty before start and reset is safe", "[modelset][lifecycle]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    REQUIRE(!modelSet.result(StageKind::Duration));
    REQUIRE(modelSet.reset(StageKind::Duration).hasValue());
    REQUIRE(!modelSet.result(StageKind::Duration));
}

TEST_CASE("ModelSet start requires a loaded stage", "[modelset][lifecycle]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    auto exp = modelSet.start(StageKind::Duration, {});
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InferenceNotInitialized));
}


TEST_CASE("ModelSet unload on non-existent model returns success", "[modelset][error]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    // unload on a non-loaded stage is a no-op and must succeed.
    REQUIRE(modelSet.unload(StageKind::Duration).hasValue());
    REQUIRE(modelSet.unload(StageKind::Vocoder).hasValue());
}

TEST_CASE("ModelSet unload is idempotent on non-loaded model", "[modelset][error]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    REQUIRE(modelSet.unload(StageKind::Acoustic).hasValue());
    // Second unload must also succeed.
    REQUIRE(modelSet.unload(StageKind::Acoustic).hasValue());
}

TEST_CASE("ModelSet unloadAll on empty stages returns success", "[modelset][error]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    REQUIRE(modelSet.unloadAll().hasValue());
}
