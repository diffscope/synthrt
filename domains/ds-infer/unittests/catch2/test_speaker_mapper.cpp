// Unit tests for ds::infer::SpeakerMapper.
//
// Regression tests for BF-22: SpeakerMapper::resolve() was changed from
// returning std::string (empty on failure) to returning
// srt::core::Expected<std::string> (error on failure). The error code is
// ErrorCode::InferenceSpeakerNotFound. Previously callers had to test for an
// empty string; they must now check the Expected for an error.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>

#include "SpeakerMapper.h"

using namespace ds::infer;

namespace {
    // Build a SpeakerMapping from a list of (singerSpeaker, modelSpeaker) pairs.
    SpeakerMapping makeMapping(
        std::initializer_list<std::pair<std::string, std::string>> entries) {
        SpeakerMapping m;
        for (const auto &e : entries) {
            m.byId[e.first] = e.second;
        }
        return m;
    }
} // namespace

// ---------------------------------------------------------------------------
// resolve success
// ---------------------------------------------------------------------------

TEST_CASE("SpeakerMapper resolve valid mapping returns expected speaker",
          "[speakermapper]") {
    SpeakerMapper mapper;
    mapper.setMapping("inference1", makeMapping({{"singer1", "model_speaker_1"}}));

    auto exp = mapper.resolve("inference1", "singer1");
    REQUIRE(exp.hasValue());
    REQUIRE(*exp == "model_speaker_1");
}

TEST_CASE("SpeakerMapper resolve different mapping returns correct speaker",
          "[speakermapper]") {
    SpeakerMapper mapper;
    mapper.setMapping("inferenceX",
                      makeMapping({{"singerA", "model_speaker_A"},
                                   {"singerB", "model_speaker_B"}}));

    REQUIRE(*mapper.resolve("inferenceX", "singerA") == "model_speaker_A");
    REQUIRE(*mapper.resolve("inferenceX", "singerB") == "model_speaker_B");
}

// ---------------------------------------------------------------------------
// BF-22 regression: resolve failure returns Error with InferenceSpeakerNotFound
// ---------------------------------------------------------------------------

TEST_CASE("SpeakerMapper resolve invalid inferenceId returns error",
          "[speakermapper][bf-22]") {
    SpeakerMapper mapper;
    // No mapping installed for any inference id.

    auto exp = mapper.resolve("nonexistent_inference", "singer1");
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.errorCode() == srt::core::ErrorCode::InferenceSpeakerNotFound);
}

TEST_CASE("SpeakerMapper resolve invalid singerSpeaker returns error",
          "[speakermapper][bf-22]") {
    SpeakerMapper mapper;
    mapper.setMapping("inference1", makeMapping({{"singer1", "model_speaker_1"}}));

    // Valid inferenceId, but singerSpeaker not present in the mapping table.
    auto exp = mapper.resolve("inference1", "unknown_singer");
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.errorCode() == srt::core::ErrorCode::InferenceSpeakerNotFound);
}

TEST_CASE("SpeakerMapper resolve empty inferenceId returns error",
          "[speakermapper][bf-22]") {
    SpeakerMapper mapper;
    // No mapping registered under the empty string key.

    auto exp = mapper.resolve(std::string{}, "singer1");
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.errorCode() == srt::core::ErrorCode::InferenceSpeakerNotFound);
}

// ---------------------------------------------------------------------------
// Mapping configuration
// ---------------------------------------------------------------------------

TEST_CASE("SpeakerMapper add mapping then resolve succeeds",
          "[speakermapper]") {
    SpeakerMapper mapper;

    // Before installing a mapping, resolve must fail.
    REQUIRE(!mapper.resolve("inference1", "singer1").hasValue());

    // After installing, resolve must succeed and return the mapped speaker id.
    mapper.setMapping("inference1", makeMapping({{"singer1", "model_speaker_1"}}));
    auto exp = mapper.resolve("inference1", "singer1");
    REQUIRE(exp.hasValue());
    REQUIRE(*exp == "model_speaker_1");
}

TEST_CASE("SpeakerMapper multiple inferenceIds resolve independently",
          "[speakermapper]") {
    SpeakerMapper mapper;
    mapper.setMapping("inference1", makeMapping({{"singer1", "model_1"}}));
    mapper.setMapping("inference2", makeMapping({{"singer1", "model_2"}}));
    mapper.setMapping("inference3", makeMapping({{"singerA", "model_3"}}));

    // Same singerSpeaker maps to different model speakers per inference.
    REQUIRE(*mapper.resolve("inference1", "singer1") == "model_1");
    REQUIRE(*mapper.resolve("inference2", "singer1") == "model_2");
    REQUIRE(*mapper.resolve("inference3", "singerA") == "model_3");

    // singer1 is not registered for inference3 -> error (BF-22 regression).
    auto exp = mapper.resolve("inference3", "singer1");
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.errorCode() == srt::core::ErrorCode::InferenceSpeakerNotFound);
}

TEST_CASE("SpeakerMapper setMapping replaces existing mapping",
          "[speakermapper]") {
    SpeakerMapper mapper;
    mapper.setMapping("inference1", makeMapping({{"singer1", "model_old"}}));

    // Re-installing the same inferenceId replaces the prior mapping.
    mapper.setMapping("inference1", makeMapping({{"singer1", "model_new"}}));

    REQUIRE(*mapper.resolve("inference1", "singer1") == "model_new");
}
