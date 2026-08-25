#include <array>
#include <functional>
#include <stdexcept>
#include <string>

#include <synthrt/Support/JSON.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

#include <AcousticInputParser.h>
#include <DurationInputParser.h>
#include <PitchInputParser.h>
#include <VarianceInputParser.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {

    namespace Co = ds::Api::Common::L1;

    srt::JsonObject parseObject(const std::string &text) {
        stdc::json::ParseError error;
        auto value = srt::JsonValue::fromJson(text, true, &error);
        if (error || !value.isObject()) {
            throw std::runtime_error("failed to parse test JSON object");
        }
        return value.toObject();
    }

    srt::JsonObject completeInput() {
        return parseObject(R"({
            "duration": 1.5,
            "steps": 20,
            "depth": 0.75,
            "words": [{
                "phones": [{
                    "token": "a",
                    "language": "en",
                    "tone": 2,
                    "start": 0.25,
                    "speakers": [{"name": "main", "proportion": 0.8}]
                }],
                "notes": [{
                    "key": "C4+75",
                    "cents": 25,
                    "duration": 1.5,
                    "glide": "up",
                    "is_rest": false
                }]
            }],
            "parameters": [
                {
                    "tag": "pitch",
                    "dynamic": true,
                    "values": [60.0, 60.5],
                    "interval": 0.005,
                    "retake": {"start": 0.2, "end": 0.8}
                },
                {"tag": "energy", "value": 1.0},
                {"tag": "future-extension", "value": 2.0}
            ],
            "speakers": [{
                "name": "main",
                "dynamic": true,
                "values": [1.0, 0.9],
                "interval": 0.005
            }]
        })");
    }

}

BOOST_AUTO_TEST_SUITE(test_InputParser)

BOOST_AUTO_TEST_CASE(test_ParsesSharedInputProtocolForEveryInferenceType) {
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

    auto durationResult = ds::parseDurationStartInput(obj);
    BOOST_REQUIRE(durationResult);
    const auto duration = durationResult.take();
    BOOST_CHECK_CLOSE(duration->duration, 1.5, 0.001);
    BOOST_REQUIRE_EQUAL(duration->words.size(), 1u);

    auto pitchResult = ds::parsePitchStartInput(obj);
    BOOST_REQUIRE(pitchResult);
    const auto pitch = pitchResult.take();
    BOOST_CHECK_EQUAL(pitch->steps, 20);
    BOOST_REQUIRE_EQUAL(pitch->parameters.size(), 1u);
    BOOST_CHECK(pitch->parameters[0].tag == Co::Tags::Pitch);

    auto varianceResult = ds::parseVarianceStartInput(obj);
    BOOST_REQUIRE(varianceResult);
    const auto variance = varianceResult.take();
    BOOST_CHECK_EQUAL(variance->steps, 20);
    BOOST_REQUIRE_EQUAL(variance->parameters.size(), 2u);
}

BOOST_AUTO_TEST_CASE(test_RejectsMalformedSharedFields) {
    using Parser = std::function<srt::Expected<void>(const srt::JsonObject &)>;
    const std::array<Parser, 4> parsers{
        [](const srt::JsonObject &obj) -> srt::Expected<void> {
            auto result = ds::parseAcousticStartInput(obj);
            return result ? srt::Expected<void>() : result.takeError();
        },
        [](const srt::JsonObject &obj) -> srt::Expected<void> {
            auto result = ds::parseDurationStartInput(obj);
            return result ? srt::Expected<void>() : result.takeError();
        },
        [](const srt::JsonObject &obj) -> srt::Expected<void> {
            auto result = ds::parsePitchStartInput(obj);
            return result ? srt::Expected<void>() : result.takeError();
        },
        [](const srt::JsonObject &obj) -> srt::Expected<void> {
            auto result = ds::parseVarianceStartInput(obj);
            return result ? srt::Expected<void>() : result.takeError();
        }};
    for (const auto &parser : parsers) {
        auto result = parser(parseObject(R"({"duration": "invalid"})"));
        BOOST_REQUIRE(!result);
        BOOST_CHECK(result.error().code() == srt::Error::InvalidFormat);
    }

    auto invalidGlide = ds::parseAcousticStartInput(
        parseObject(R"({"words": [{"notes": [{"glide": "sideways"}]}]})"));
    BOOST_REQUIRE(!invalidGlide);
    BOOST_CHECK(invalidGlide.error().code() == srt::Error::InvalidFormat);

    auto invalidProportion = ds::parsePitchStartInput(parseObject(
        R"({"words": [{"phones": [{"speakers": [{"name": "main", "proportion": "all"}]}]}]})"));
    BOOST_REQUIRE(!invalidProportion);
    BOOST_CHECK(invalidProportion.error().code() == srt::Error::InvalidFormat);

    auto invalidCurve = ds::parseVarianceStartInput(parseObject(
        R"({"parameters": [{"tag": "energy", "dynamic": true, "values": [1], "interval": 0}]})"));
    BOOST_REQUIRE(!invalidCurve);
    BOOST_CHECK(invalidCurve.error().code() == srt::Error::InvalidFormat);
}

BOOST_AUTO_TEST_SUITE_END()
