#include <inputparser/DurationInputParser.h>

#define BOOST_TEST_MAIN
#include "TestSupport.h"

BOOST_AUTO_TEST_CASE(parses_shared_input_protocol) {
    const auto obj = completeInput();
    auto durationResult = ds::parseDurationStartInput(obj);
    BOOST_REQUIRE(durationResult);
    const auto duration = durationResult.take();
    BOOST_CHECK_CLOSE(duration->duration, 1.5, 0.001);
    BOOST_REQUIRE_EQUAL(duration->words.size(), 1u);
}

BOOST_AUTO_TEST_CASE(rejects_malformed_input) {
    auto invalid = ds::parseDurationStartInput(parseObject(R"({"duration":"invalid"})"));
    BOOST_REQUIRE(!invalid);
    BOOST_CHECK(invalid.error().code() == srt::Error::InvalidFormat);
}

BOOST_AUTO_TEST_CASE(rejects_malformed_nested_score_fields_with_their_paths) {
    checkMalformedWordInputs(ds::parseDurationStartInput);
}
