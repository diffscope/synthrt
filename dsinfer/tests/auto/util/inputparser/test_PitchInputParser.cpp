#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

#include <inputparser/PitchInputParser.h>

#define BOOST_TEST_MAIN
#include "TestSupport.h"

namespace Co = ds::Api::Common::L1;

BOOST_AUTO_TEST_CASE(parses_shared_input_protocol) {
    const auto obj = completeInput();
    auto pitchResult = ds::parsePitchStartInput(obj);
    BOOST_REQUIRE(pitchResult);
    const auto pitch = pitchResult.take();
    BOOST_CHECK_EQUAL(pitch->steps, 20);
    BOOST_REQUIRE_EQUAL(pitch->parameters.size(), 1u);
    BOOST_CHECK(pitch->parameters[0].tag == Co::Tags::Pitch);
}

BOOST_AUTO_TEST_CASE(rejects_malformed_input) {
    auto invalid = ds::parsePitchStartInput(parseObject(R"({"duration":"invalid"})"));
    BOOST_REQUIRE(!invalid);
    BOOST_CHECK(invalid.error().code() == srt::Error::InvalidFormat);
    auto bad = ds::parsePitchStartInput(parseObject(
        R"({"words":[{"phones":[{"speakers":[{"name":"main","proportion":"all"}]}]}]})"));
    BOOST_REQUIRE(!bad);
    BOOST_CHECK(bad.error().code() == srt::Error::InvalidFormat);
}

BOOST_AUTO_TEST_CASE(rejects_malformed_nested_score_fields_with_their_paths) {
    checkMalformedWordInputs(ds::parsePitchStartInput);
}

BOOST_AUTO_TEST_CASE(rejects_malformed_parameter_and_speaker_curves_with_their_paths) {
    checkMalformedCurveInputs(ds::parsePitchStartInput);
}
