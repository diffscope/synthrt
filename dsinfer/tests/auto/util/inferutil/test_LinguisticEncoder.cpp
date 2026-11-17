#include <set>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <inferutil/LinguisticEncoder.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(word_and_phoneme_encoders_have_distinct_input_signatures) {
    namespace Co = ds::Api::Common::L1;
    Co::InputWordInfo word;
    word.phones = {
        {"a", "eng", 0, 0  },
        {"b", "eng", 0, 0.2}
    };
    word.notes = {
        {60, 0, 0.4}
    };
    const std::vector<Co::InputWordInfo> words{word};
    const std::map<std::string, int> tokens{
        {"a", 1},
        {"b", 2}
    };
    auto phoneme = ds::inferutil::preprocessLinguisticPhoneme(words, tokens, {}, false, 0.1);
    BOOST_REQUIRE(phoneme);
    BOOST_CHECK_EQUAL(phoneme.value()->inputs.size(), 2u);
    BOOST_CHECK_EQUAL(phoneme.value()->inputs.count("languages"), 0u);
    BOOST_CHECK_EQUAL(phoneme.value()->inputs.at("ph_dur")->data<int64_t>()[0], 2);
    BOOST_CHECK(phoneme.value()->outputs == std::set<std::string>({"encoder_out", "x_masks"}));
    auto byWord = ds::inferutil::preprocessLinguisticWord(words, tokens,
                                                          {
                                                              {"eng", 7}
    },
                                                          true, 0.1);
    BOOST_REQUIRE(byWord);
    BOOST_CHECK_EQUAL(byWord.value()->inputs.size(), 4u);
    BOOST_CHECK_EQUAL(byWord.value()->inputs.at("word_div")->data<int64_t>()[0], 2);
    BOOST_CHECK_EQUAL(byWord.value()->inputs.at("word_dur")->data<int64_t>()[0], 4);
    BOOST_CHECK_EQUAL(byWord.value()->inputs.at("languages")->data<int64_t>()[0], 7);
    BOOST_CHECK(!ds::inferutil::preprocessLinguisticWord(words, {}, {}, false, 0.1));
    BOOST_CHECK(!ds::inferutil::preprocessLinguisticPhoneme(words, tokens, {}, true, 0.1));
}
