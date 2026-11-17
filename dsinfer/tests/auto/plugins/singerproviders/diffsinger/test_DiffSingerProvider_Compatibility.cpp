#define BOOST_TEST_MAIN
#include "TestSupport.h"

BOOST_AUTO_TEST_SUITE(test_DiffSingerProvider_Compatibility)

BOOST_AUTO_TEST_CASE(test_compatible_acoustic_and_vocoder_commit) {
    TemporaryDirectory temporary;
    writePackage(temporary.path(), 44100, 44100);
    auto unit = makeUnit();

    auto opened = unit.openPackage(temporary.path(), srt::SynthUnit::Load);

    BOOST_REQUIRE(opened);
    BOOST_CHECK_EQUAL(unit.loadedPackages().size(), 1u);
}

BOOST_AUTO_TEST_CASE(test_incompatible_acoustic_and_vocoder_do_not_commit) {
    TemporaryDirectory temporary;
    writePackage(temporary.path(), 44100, 48000);
    auto unit = makeUnit();

    auto opened = unit.openPackage(temporary.path(), srt::SynthUnit::Load);

    BOOST_CHECK(!opened);
    BOOST_CHECK(opened.error().toString().find("sampleRate") != std::string::npos);
    BOOST_CHECK(unit.loadedPackages().empty());
}

BOOST_AUTO_TEST_CASE(test_missing_required_inference_role_does_not_commit) {
    TemporaryDirectory temporary;
    writePackage(temporary.path(), 44100, 44100, false, true);
    auto unit = makeUnit();

    auto opened = unit.openPackage(temporary.path(), srt::SynthUnit::Load);

    BOOST_CHECK(!opened);
    BOOST_CHECK(opened.error().toString().find("acoustic inference import") != std::string::npos);
    BOOST_CHECK(unit.loadedPackages().empty());
}

BOOST_AUTO_TEST_CASE(test_vocoder_rejects_non_acoustic_compatibility_source) {
    TemporaryDirectory temporary;
    writePackage(temporary.path(), 44100, 44100);
    auto unit = makeUnit();

    auto opened = unit.openPackage(temporary.path(), srt::SynthUnit::Load);

    BOOST_REQUIRE(opened);
    auto package = opened.take();
    auto vocoderSpec = package.contribution("inference", "vocoder");
    auto pitchSpec = package.contribution("inference", "pitch");
    BOOST_REQUIRE(vocoderSpec);
    BOOST_REQUIRE(pitchSpec);
    auto vocoder = vocoderSpec->as<srt::InferenceSpec>();
    auto pitch = pitchSpec->as<srt::InferenceSpec>();
    BOOST_REQUIRE(vocoder);
    BOOST_REQUIRE(pitch);
    auto compatible = vocoder->validateCompatibilityWith(*pitch);
    BOOST_REQUIRE(!compatible);
    BOOST_CHECK(compatible.error().code() == srt::Error::InvalidArgument);
}

// A singer may declare reserved phonemes, which a lyric contains directly and no language
// produces. The provider checks each reserved phoneme at load time against the phoneme table of
// every imported model, because a token absent from a model produces silence and no later stage
// detects the error.
BOOST_AUTO_TEST_CASE(test_reserved_phonemes_present_in_every_model_commit) {
    TemporaryDirectory temporary;
    writePackage(temporary.path(), 44100, 44100, true, true, R"(["SP"])");
    auto unit = makeUnit();
    auto opened = unit.openPackage(temporary.path(), srt::SynthUnit::Load);
    const std::string why = opened ? std::string() : opened.error().toString();
    BOOST_REQUIRE_MESSAGE(bool(opened), why);
}

BOOST_AUTO_TEST_CASE(test_reserved_phoneme_missing_from_a_model_does_not_commit) {
    TemporaryDirectory temporary;
    writePackage(temporary.path(), 44100, 44100, true, true, R"(["SP","ZZ"])");
    auto unit = makeUnit();
    auto opened = unit.openPackage(temporary.path(), srt::SynthUnit::Load);
    BOOST_REQUIRE(!opened);
    const auto message = opened.error().toString();
    BOOST_CHECK_MESSAGE(message.find("ZZ") != std::string::npos, message);
}

BOOST_AUTO_TEST_CASE(test_reserved_phonemes_must_be_distinct_non_empty_strings) {
    TemporaryDirectory temporary;
    writePackage(temporary.path(), 44100, 44100, true, true, R"(["SP","SP"])");
    auto unit = makeUnit();
    BOOST_CHECK(!unit.openPackage(temporary.path(), srt::SynthUnit::Load));
}

BOOST_AUTO_TEST_SUITE_END()
