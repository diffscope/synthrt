#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <inferutil/InputWord.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace Co = ds::Api::Common::L1;
using namespace ds::inferutil;

BOOST_AUTO_TEST_CASE(token_language_resolution_and_frame_budgets) {
    Co::InputWordInfo word;
    word.phones = {
        {"a",  "eng", 0, 0  },
        {"b",  "eng", 0, 0.2},
        {"SP", "eng", 0, 0.4}
    };
    word.notes = {
        {60, 0, 0.3},
        {62, 0, 0.3}
    };
    const std::vector<Co::InputWordInfo> words{word};
    BOOST_CHECK_EQUAL(getPhoneCount(words), 3u);
    BOOST_CHECK_EQUAL(getNoteCount(words), 2u);
    BOOST_CHECK_CLOSE(getWordDuration(word), 0.6, 0.0001);
    auto tokens = preprocessPhonemeTokens(words, {
                                                     {"eng/a", 8},
                                                     {"a",     1},
                                                     {"b",     2},
                                                     {"SP",    0}
    });
    BOOST_REQUIRE(tokens);
    const auto data = tokens.value()->data<int64_t>();
    BOOST_REQUIRE(data);
    BOOST_CHECK_EQUAL(data[0], 8);
    BOOST_CHECK_EQUAL(data[1], 2);
    BOOST_CHECK_EQUAL(data[2], 0);
    auto languages = preprocessPhonemeLanguages(words, {
                                                           {"eng", 4}
    });
    BOOST_REQUIRE(languages);
    BOOST_CHECK_EQUAL(languages.value()->data<int64_t>()[2], 4);
    int64_t length = -1;
    auto durations = preprocessPhonemeDurations(words, 0.1, &length);
    BOOST_REQUIRE(durations);
    BOOST_CHECK_EQUAL(length, 6);
    for (int i = 0; i < 3; ++i)
        BOOST_CHECK_EQUAL(durations.value()->data<int64_t>()[i], 2);
    auto unknownToken = preprocessPhonemeTokens(words, {});
    BOOST_REQUIRE(!unknownToken);
    BOOST_CHECK(unknownToken.error().code() == srt::Error::InvalidArgument);
    auto unknownLanguage = preprocessPhonemeLanguages(words, {});
    BOOST_REQUIRE(!unknownLanguage);
    BOOST_CHECK(unknownLanguage.error().code() == srt::Error::InvalidArgument);
}
