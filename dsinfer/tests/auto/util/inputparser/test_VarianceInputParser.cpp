#include <inputparser/VarianceInputParser.h>

#define BOOST_TEST_MAIN
#include "TestSupport.h"

BOOST_AUTO_TEST_CASE(parses_shared_input_protocol) {
    const auto obj = completeInput();
    auto varianceResult = ds::parseVarianceStartInput(obj);
    BOOST_REQUIRE(varianceResult);
    const auto variance = varianceResult.take();
    BOOST_CHECK_EQUAL(variance->steps, 20);
    BOOST_REQUIRE_EQUAL(variance->parameters.size(), 2u);
}

BOOST_AUTO_TEST_CASE(rejects_malformed_input) {
    auto invalid = ds::parseVarianceStartInput(parseObject(R"({"duration":"invalid"})"));
    BOOST_REQUIRE(!invalid);
    BOOST_CHECK(invalid.error().code() == srt::Error::InvalidFormat);
    auto bad = ds::parseVarianceStartInput(parseObject(
        R"({"parameters":[{"tag":"energy","dynamic":true,"values":[1],"interval":0}]})"));
    BOOST_REQUIRE(!bad);
    BOOST_CHECK(bad.error().code() == srt::Error::InvalidFormat);
}
