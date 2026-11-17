#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

#include <inputparser/AcousticInputParser.h>

#define BOOST_TEST_MAIN
#include "TestSupport.h"

namespace Co = ds::Api::Common::L1;

BOOST_AUTO_TEST_CASE(parses_shared_input_protocol) {
    const auto obj = completeInput();
    auto acousticResult = ds::parseAcousticStartInput(obj);
    BOOST_REQUIRE(acousticResult);
    const auto acoustic = acousticResult.take();
    BOOST_CHECK_CLOSE(acoustic->duration, 1.5, 0.001);
    BOOST_CHECK_EQUAL(acoustic->steps, 20);
    BOOST_CHECK_CLOSE(acoustic->depth, 0.75f, 0.001f);
    BOOST_REQUIRE_EQUAL(acoustic->words.size(), 1u);
    BOOST_REQUIRE_EQUAL(acoustic->words[0].phones.size(), 1u);
    BOOST_CHECK_EQUAL(acoustic->words[0].phones[0].token, "a");
    BOOST_REQUIRE_EQUAL(acoustic->words[0].phones[0].speakers.size(), 1u);
    BOOST_CHECK_CLOSE(acoustic->words[0].phones[0].speakers[0].proportion, 0.8, 0.001);
    BOOST_REQUIRE_EQUAL(acoustic->words[0].notes.size(), 1u);
    BOOST_CHECK_EQUAL(acoustic->words[0].notes[0].key, 61);
    BOOST_CHECK_EQUAL(acoustic->words[0].notes[0].cents, 0);
    BOOST_CHECK(acoustic->words[0].notes[0].glide == Co::GlideType::Up);
    BOOST_REQUIRE_EQUAL(acoustic->parameters.size(), 2u);
    BOOST_CHECK(acoustic->parameters[0].tag == Co::Tags::Pitch);
    BOOST_CHECK(acoustic->parameters[0].retake.has_value());
    BOOST_CHECK(acoustic->parameters[1].tag == Co::Tags::Energy);
    BOOST_REQUIRE_EQUAL(acoustic->speakers.size(), 1u);
    BOOST_CHECK_EQUAL(acoustic->speakers[0].name, "main");
}

BOOST_AUTO_TEST_CASE(rejects_malformed_input) {
    auto invalid = ds::parseAcousticStartInput(parseObject(R"({"duration":"invalid"})"));
    BOOST_REQUIRE(!invalid);
    BOOST_CHECK(invalid.error().code() == srt::Error::InvalidFormat);
    auto bad =
        ds::parseAcousticStartInput(parseObject(R"({"words":[{"notes":[{"glide":"sideways"}]}]})"));
    BOOST_REQUIRE(!bad);
    BOOST_CHECK(bad.error().code() == srt::Error::InvalidFormat);
}

BOOST_AUTO_TEST_CASE(rejects_malformed_nested_score_fields_with_their_paths) {
    checkMalformedWordInputs(ds::parseAcousticStartInput);
}

BOOST_AUTO_TEST_CASE(rejects_malformed_parameter_and_speaker_curves_with_their_paths) {
    checkMalformedCurveInputs(ds::parseAcousticStartInput);
}
