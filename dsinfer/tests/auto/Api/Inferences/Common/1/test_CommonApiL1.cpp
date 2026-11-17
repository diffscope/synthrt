#include <utility>
#include <vector>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace Api = ds::Api::Common::L1;

BOOST_AUTO_TEST_CASE(score_values_own_their_nested_notes_and_speaker_mixtures) {
    Api::InputPhonemeInfo phone;
    phone.token = "a";
    phone.language = "eng";
    phone.speakers.push_back({"lead", 0.75});
    Api::InputWordInfo original;
    original.phones.push_back(phone);
    original.notes.push_back({60, 25, 0.5, Api::GlideType::Up, false});
    auto copied = original;
    original.phones[0].speakers[0].name = "other";
    original.notes[0].key = 72;
    BOOST_CHECK_EQUAL(copied.phones[0].speakers[0].name, "lead");
    BOOST_CHECK_EQUAL(copied.notes[0].key, 60);
    auto moved = std::move(copied);
    BOOST_CHECK_EQUAL(moved.phones[0].token, "a");
    BOOST_CHECK_EQUAL(moved.notes[0].duration, 0.5);
}

BOOST_AUTO_TEST_CASE(control_curves_keep_retake_ranges_and_speaker_proportions) {
    Api::InputParameterInfo curve;
    curve.tag = Api::Tags::Energy;
    curve.interval = 0.01;
    curve.values = {0.5, 0.75};
    curve.retake = Api::InputParameterInfo::RetakeRange{0.1, 0.2};
    auto copy = curve;
    curve.values[0] = 0;
    BOOST_CHECK_EQUAL(copy.values[0], 0.5);
    BOOST_REQUIRE(copy.retake);
    BOOST_CHECK_EQUAL(copy.retake->start, 0.1);
    BOOST_CHECK_EQUAL(copy.retake->end, 0.2);
    Api::InputSpeakerInfo speaker{
        "lead", 0.01, {0.25, 0.75}
    };
    auto moved = std::move(speaker);
    BOOST_CHECK_EQUAL(moved.name, "lead");
    BOOST_CHECK(moved.proportions == std::vector<double>({0.25, 0.75}));
}
