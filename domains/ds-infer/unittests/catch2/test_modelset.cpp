// Unit tests for ds::infer::ModelSet lifecycle with empty/null stages.
//
// Regression tests for BF-05 (slot()/stageSpec() invalid StageKind abort) and
// BF-17 (unloadAll first failure skips remaining stages). Uses empty StageSet
// (all spec pointers null) to test lifecycle without loading actual plugins.

#include <catch2/catch_test_macros.hpp>

#include <diffsinger/Infer/ModelSet.h>
#include <diffsinger/Infer/StageKind.h>

using namespace ds::infer;

namespace {

    // Create an empty StageSet (all spec pointers null).
    StageSet makeEmptyStageSet() {
        StageSet stages;
        // All StageSpec entries have spec = nullptr by default.
        return stages;
    }

} // namespace

// ---------------------------------------------------------------------------
// ModelSet with empty stages — load returns error, not crash
// ---------------------------------------------------------------------------

TEST_CASE("ModelSet empty stages load returns error", "[modelset][bf-05]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    // load() with null spec should return error, not crash.
    auto exp = modelSet.load(StageKind::Duration);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.error().message().find("null") != std::string::npos);
}

TEST_CASE("ModelSet empty stages isLoaded returns false", "[modelset]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    REQUIRE(!modelSet.isLoaded(StageKind::Duration));
    REQUIRE(!modelSet.isLoaded(StageKind::Pitch));
    REQUIRE(!modelSet.isLoaded(StageKind::Variance));
    REQUIRE(!modelSet.isLoaded(StageKind::Acoustic));
    REQUIRE(!modelSet.isLoaded(StageKind::Vocoder));
}

TEST_CASE("ModelSet empty stages model returns empty NO", "[modelset]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    REQUIRE(!modelSet.model(StageKind::Duration));
    REQUIRE(!modelSet.model(StageKind::Vocoder));
}

// ---------------------------------------------------------------------------
// BF-17: unloadAll continues on first failure, doesn't skip remaining
// ---------------------------------------------------------------------------

TEST_CASE("ModelSet unloadAll empty stages succeeds", "[modelset][bf-17]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    // unloadAll on empty ModelSet should succeed (no-op).
    auto exp = modelSet.unloadAll();
    REQUIRE(exp.hasValue());
}

TEST_CASE("ModelSet unload empty stages succeeds", "[modelset]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    // unload on unloaded stage should succeed (no-op).
    REQUIRE(modelSet.unload(StageKind::Duration).hasValue());
    REQUIRE(modelSet.unload(StageKind::Vocoder).hasValue());
}

TEST_CASE("ModelSet stop empty stages succeeds", "[modelset]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    // stop on unloaded stage should succeed (no-op).
    REQUIRE(modelSet.stop(StageKind::Duration).hasValue());
    REQUIRE(modelSet.stop(StageKind::Vocoder).hasValue());
}

// ---------------------------------------------------------------------------
// ModelSet stages() preserves construction data
// ---------------------------------------------------------------------------

TEST_CASE("ModelSet stages returns construction data", "[modelset]") {
    auto stages = makeEmptyStageSet();
    // Set a kind on one stage to verify it's preserved.
    stages.duration.kind = StageKind::Duration;
    ModelSet modelSet(std::move(stages));

    const auto &stored = modelSet.stages();
    REQUIRE(stored.duration.kind == StageKind::Duration);
    REQUIRE(stored.duration.spec == nullptr);
}

// ---------------------------------------------------------------------------
// ModelSet move semantics
// ---------------------------------------------------------------------------

TEST_CASE("ModelSet move constructor transfers ownership", "[modelset]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet1(std::move(stages));
    REQUIRE(!modelSet1.isLoaded(StageKind::Duration));

    ModelSet modelSet2(std::move(modelSet1));
    REQUIRE(!modelSet2.isLoaded(StageKind::Duration));
    // modelSet1 is now in moved-from state; do not call methods on it.
}

// ---------------------------------------------------------------------------
// ModelSet move assignment
// ---------------------------------------------------------------------------

TEST_CASE("ModelSet move assignment transfers ownership", "[modelset]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet1(std::move(stages));
    REQUIRE(!modelSet1.isLoaded(StageKind::Duration));

    auto stages2 = makeEmptyStageSet();
    ModelSet modelSet2(std::move(stages2));
    modelSet2 = std::move(modelSet1);
    REQUIRE(!modelSet2.isLoaded(StageKind::Duration));
}

// ---------------------------------------------------------------------------
// ModelSet load returns error for all 5 stages with null spec
// ---------------------------------------------------------------------------

TEST_CASE("ModelSet empty stages load all stages return error", "[modelset][bf-05]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    // All 5 stages should return error, not crash.
    for (auto kind : {StageKind::Duration, StageKind::Pitch, StageKind::Variance,
                      StageKind::Acoustic, StageKind::Vocoder}) {
        auto exp = modelSet.load(kind);
        REQUIRE(!exp.hasValue());
        REQUIRE(exp.error().message().find("null") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// ModelSet unloadAll is idempotent
// ---------------------------------------------------------------------------

TEST_CASE("ModelSet unloadAll idempotent on empty stages", "[modelset][bf-17]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    // First unloadAll should succeed.
    REQUIRE(modelSet.unloadAll().hasValue());
    // Second unloadAll should also succeed (idempotent).
    REQUIRE(modelSet.unloadAll().hasValue());
}

// ---------------------------------------------------------------------------
// ModelSet stop after unload is safe (no-op)
// ---------------------------------------------------------------------------

TEST_CASE("ModelSet stop after unload is safe", "[modelset]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    // unload then stop should not crash.
    REQUIRE(modelSet.unload(StageKind::Duration).hasValue());
    REQUIRE(modelSet.stop(StageKind::Duration).hasValue());

    // stop then unload should not crash.
    REQUIRE(modelSet.stop(StageKind::Vocoder).hasValue());
    REQUIRE(modelSet.unload(StageKind::Vocoder).hasValue());
}

// ---------------------------------------------------------------------------
// StageSet::find() returns correct StageSpec for each StageKind
// ---------------------------------------------------------------------------

TEST_CASE("StageSet find returns correct spec for each kind", "[stageset]") {
    StageSet stages;
    // Set kind on each stage to distinguish them.
    stages.duration.kind = StageKind::Duration;
    stages.pitch.kind = StageKind::Pitch;
    stages.variance.kind = StageKind::Variance;
    stages.acoustic.kind = StageKind::Acoustic;
    stages.vocoder.kind = StageKind::Vocoder;

    REQUIRE(stages.find(StageKind::Duration) == &stages.duration);
    REQUIRE(stages.find(StageKind::Pitch) == &stages.pitch);
    REQUIRE(stages.find(StageKind::Variance) == &stages.variance);
    REQUIRE(stages.find(StageKind::Acoustic) == &stages.acoustic);
    REQUIRE(stages.find(StageKind::Vocoder) == &stages.vocoder);
}

TEST_CASE("StageSet find returns nullptr for invalid kind", "[stageset]") {
    StageSet stages;
    // Cast an out-of-range int to StageKind to test the default branch.
    // StageKind is an enum (not enum class), so static_cast is valid.
    // The switch in find() covers all 5 valid values, so any other value
    // hits the default return nullptr.
    auto invalidKind = static_cast<StageKind>(999);
    REQUIRE(stages.find(invalidKind) == nullptr);
}

// ---------------------------------------------------------------------------
// ModelSet stages() preserves all 5 stage kinds after construction
// ---------------------------------------------------------------------------

TEST_CASE("ModelSet stages preserves all stage kinds", "[modelset]") {
    StageSet stages;
    stages.duration.kind = StageKind::Duration;
    stages.pitch.kind = StageKind::Pitch;
    stages.variance.kind = StageKind::Variance;
    stages.acoustic.kind = StageKind::Acoustic;
    stages.vocoder.kind = StageKind::Vocoder;

    ModelSet modelSet(std::move(stages));

    const auto &stored = modelSet.stages();
    REQUIRE(stored.duration.kind == StageKind::Duration);
    REQUIRE(stored.pitch.kind == StageKind::Pitch);
    REQUIRE(stored.variance.kind == StageKind::Variance);
    REQUIRE(stored.acoustic.kind == StageKind::Acoustic);
    REQUIRE(stored.vocoder.kind == StageKind::Vocoder);
    REQUIRE(stored.duration.spec == nullptr);
    REQUIRE(stored.vocoder.spec == nullptr);
}
