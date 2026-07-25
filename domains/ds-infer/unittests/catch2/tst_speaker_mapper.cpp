// Unit tests for ds::infer::SpeakerMapper.
//
// Regression tests for BF-22: SpeakerMapper::resolve() was changed from
// returning std::string (empty on failure) to returning
// srt::core::Expected<std::string> (error on failure). The error code is
// ErrorCode::InferenceSpeakerNotFound. Previously callers had to test for an
// empty string; they must now check the Expected for an error.
//
// BF-31: SpeakerMapper now keys by (packageId, inferenceId) so that two
// packages defining inferences with the same id keep independent speaker
// tables (ARCH-06 cross-package stage sharing).

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

    // Default package id used by non-cross-package tests.
    constexpr const char *kPkg = "com.test.default";
} // namespace

// ---------------------------------------------------------------------------
// resolve success
// ---------------------------------------------------------------------------

TEST_CASE("SpeakerMapper resolve valid mapping returns expected speaker",
          "[speakermapper]") {
    SpeakerMapper mapper;
    mapper.setMapping(kPkg, "inference1", makeMapping({{"singer1", "model_speaker_1"}}));

    auto exp = mapper.resolve(kPkg, "inference1", "singer1");
    REQUIRE(exp.hasValue());
    REQUIRE(*exp == "model_speaker_1");
}

TEST_CASE("SpeakerMapper resolve different mapping returns correct speaker",
          "[speakermapper]") {
    SpeakerMapper mapper;
    mapper.setMapping(kPkg, "inferenceX",
                      makeMapping({{"singerA", "model_speaker_A"},
                                   {"singerB", "model_speaker_B"}}));

    REQUIRE(*mapper.resolve(kPkg, "inferenceX", "singerA") == "model_speaker_A");
    REQUIRE(*mapper.resolve(kPkg, "inferenceX", "singerB") == "model_speaker_B");
}

// ---------------------------------------------------------------------------
// BF-22 regression: resolve failure returns Error with InferenceSpeakerNotFound
// ---------------------------------------------------------------------------

TEST_CASE("SpeakerMapper resolve invalid inferenceId returns error",
          "[speakermapper][bf-22]") {
    SpeakerMapper mapper;
    // No mapping installed for any inference id.

    auto exp = mapper.resolve(kPkg, "nonexistent_inference", "singer1");
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.errorCode() == srt::core::ErrorCode::InferenceSpeakerNotFound);
}

TEST_CASE("SpeakerMapper resolve invalid singerSpeaker returns error",
          "[speakermapper][bf-22]") {
    SpeakerMapper mapper;
    mapper.setMapping(kPkg, "inference1", makeMapping({{"singer1", "model_speaker_1"}}));

    // Valid inferenceId, but singerSpeaker not present in the mapping table.
    auto exp = mapper.resolve(kPkg, "inference1", "unknown_singer");
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.errorCode() == srt::core::ErrorCode::InferenceSpeakerNotFound);
}

TEST_CASE("SpeakerMapper resolve empty inferenceId returns error",
          "[speakermapper][bf-22]") {
    SpeakerMapper mapper;
    // No mapping registered under the empty string key.

    auto exp = mapper.resolve(kPkg, std::string{}, "singer1");
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
    REQUIRE(!mapper.resolve(kPkg, "inference1", "singer1").hasValue());

    // After installing, resolve must succeed and return the mapped speaker id.
    mapper.setMapping(kPkg, "inference1", makeMapping({{"singer1", "model_speaker_1"}}));
    auto exp = mapper.resolve(kPkg, "inference1", "singer1");
    REQUIRE(exp.hasValue());
    REQUIRE(*exp == "model_speaker_1");
}

TEST_CASE("SpeakerMapper multiple inferenceIds resolve independently",
          "[speakermapper]") {
    SpeakerMapper mapper;
    mapper.setMapping(kPkg, "inference1", makeMapping({{"singer1", "model_1"}}));
    mapper.setMapping(kPkg, "inference2", makeMapping({{"singer1", "model_2"}}));
    mapper.setMapping(kPkg, "inference3", makeMapping({{"singerA", "model_3"}}));

    // Same singerSpeaker maps to different model speakers per inference.
    REQUIRE(*mapper.resolve(kPkg, "inference1", "singer1") == "model_1");
    REQUIRE(*mapper.resolve(kPkg, "inference2", "singer1") == "model_2");
    REQUIRE(*mapper.resolve(kPkg, "inference3", "singerA") == "model_3");

    // singer1 is not registered for inference3 -> error (BF-22 regression).
    auto exp = mapper.resolve(kPkg, "inference3", "singer1");
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.errorCode() == srt::core::ErrorCode::InferenceSpeakerNotFound);
}

TEST_CASE("SpeakerMapper setMapping replaces existing mapping",
          "[speakermapper]") {
    SpeakerMapper mapper;
    mapper.setMapping(kPkg, "inference1", makeMapping({{"singer1", "model_old"}}));

    // Re-installing the same (packageId, inferenceId) replaces the prior mapping.
    mapper.setMapping(kPkg, "inference1", makeMapping({{"singer1", "model_new"}}));

    REQUIRE(*mapper.resolve(kPkg, "inference1", "singer1") == "model_new");
}

// ---------------------------------------------------------------------------
// BF-31: Cross-package isolation (same inferenceId, different packageId)
// ---------------------------------------------------------------------------

TEST_CASE("SpeakerMapper same inferenceId in different packages resolves independently",
          "[speakermapper][bf-31]") {
    // Two packages both define an inference with id "pitch", but each maps
    // singer "singer1" to a different model speaker. Without packageId in
    // the key, the second setMapping would silently overwrite the first.
    SpeakerMapper mapper;
    mapper.setMapping("com.test.A", "pitch", makeMapping({{"singer1", "model_A"}}));
    mapper.setMapping("com.test.B", "pitch", makeMapping({{"singer1", "model_B"}}));

    REQUIRE(*mapper.resolve("com.test.A", "pitch", "singer1") == "model_A");
    REQUIRE(*mapper.resolve("com.test.B", "pitch", "singer1") == "model_B");
}

TEST_CASE("SpeakerMapper resolve wrong packageId returns error",
          "[speakermapper][bf-31]") {
    // Mapping installed only for package A; resolving via package B must
    // fail (previously the inferenceId-only key would leak A's mapping to B).
    SpeakerMapper mapper;
    mapper.setMapping("com.test.A", "pitch", makeMapping({{"singer1", "model_A"}}));

    REQUIRE(!mapper.resolve("com.test.B", "pitch", "singer1").hasValue());
    REQUIRE(*mapper.resolve("com.test.A", "pitch", "singer1") == "model_A");
}

TEST_CASE("SpeakerMapper empty packageId does not collide with named packageId",
          "[speakermapper][bf-31]") {
    // An empty packageId is a distinct key from any named packageId.
    SpeakerMapper mapper;
    mapper.setMapping("", "pitch", makeMapping({{"singer1", "model_empty"}}));
    mapper.setMapping("com.test.A", "pitch", makeMapping({{"singer1", "model_A"}}));

    REQUIRE(*mapper.resolve("", "pitch", "singer1") == "model_empty");
    REQUIRE(*mapper.resolve("com.test.A", "pitch", "singer1") == "model_A");
}

TEST_CASE("SpeakerMapper many packages with same inferenceId resolve correctly",
          "[speakermapper][bf-31]") {
    // Stress: N packages each define inference "acoustic" with distinct
    // speaker tables. Each must resolve to its own model speaker.
    SpeakerMapper mapper;
    const int N = 8;
    for (int i = 0; i < N; ++i) {
        const auto pkg = "com.test.pkg" + std::to_string(i);
        const auto model = "model_" + std::to_string(i);
        mapper.setMapping(pkg, "acoustic", makeMapping({{"singer1", model}}));
    }
    for (int i = 0; i < N; ++i) {
        const auto pkg = "com.test.pkg" + std::to_string(i);
        const auto model = "model_" + std::to_string(i);
        REQUIRE(*mapper.resolve(pkg, "acoustic", "singer1") == model);
    }
}
