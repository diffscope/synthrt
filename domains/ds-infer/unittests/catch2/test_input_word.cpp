// Unit tests for ds::infer::inferutil::InputWord helper functions.
//
// Covers getPhoneCount, getNoteCount, getWordDuration, preprocessPhonemeTokens,
// preprocessPhonemeLanguages, and preprocessPhonemeDurations with edge cases
// including empty inputs, unknown tokens/languages, SP/AP special tokens,
// language-prefixed tokens, and multi-word phone start propagation.

#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include <inferutil/InputWord.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

using namespace ds::infer::inferutil;
namespace Co = srt::svs::Api::Common::L1;

namespace {
    bool approxEqual(double a, double b, double eps = 1e-9) {
        return std::abs(a - b) < eps;
    }
}

// ---------------------------------------------------------------------------
// getPhoneCount
// ---------------------------------------------------------------------------

TEST_CASE("getPhoneCount empty words returns zero", "[inputword]") {
    std::vector<Co::InputWordInfo> words;
    REQUIRE(getPhoneCount(words) == 0);
}

TEST_CASE("getPhoneCount single word single phone", "[inputword]") {
    std::vector<Co::InputWordInfo> words(1);
    words[0].phones.resize(1);
    REQUIRE(getPhoneCount(words) == 1);
}

TEST_CASE("getPhoneCount multiple words", "[inputword]") {
    std::vector<Co::InputWordInfo> words(3);
    words[0].phones.resize(2);
    words[1].phones.resize(0); // empty phones
    words[2].phones.resize(5);
    REQUIRE(getPhoneCount(words) == 7);
}

TEST_CASE("getPhoneCount word with empty phones", "[inputword]") {
    std::vector<Co::InputWordInfo> words(2);
    words[0].phones.resize(0);
    words[1].phones.resize(3);
    REQUIRE(getPhoneCount(words) == 3);
}

// ---------------------------------------------------------------------------
// getNoteCount
// ---------------------------------------------------------------------------

TEST_CASE("getNoteCount empty words returns zero", "[inputword]") {
    std::vector<Co::InputWordInfo> words;
    REQUIRE(getNoteCount(words) == 0);
}

TEST_CASE("getNoteCount multiple words with notes", "[inputword]") {
    std::vector<Co::InputWordInfo> words(2);
    words[0].notes.resize(3);
    words[1].notes.resize(2);
    REQUIRE(getNoteCount(words) == 5);
}

TEST_CASE("getNoteCount word with empty notes", "[inputword]") {
    std::vector<Co::InputWordInfo> words(2);
    words[0].notes.resize(0);
    words[1].notes.resize(4);
    REQUIRE(getNoteCount(words) == 4);
}

// ---------------------------------------------------------------------------
// getWordDuration
// ---------------------------------------------------------------------------

TEST_CASE("getWordDuration empty notes returns zero", "[inputword]") {
    Co::InputWordInfo word;
    REQUIRE(getWordDuration(word) == 0.0);
}

TEST_CASE("getWordDuration single note", "[inputword]") {
    Co::InputWordInfo word;
    word.notes.resize(1);
    word.notes[0].duration = 0.5;
    REQUIRE(getWordDuration(word) == 0.5);
}

TEST_CASE("getWordDuration multiple notes sum", "[inputword]") {
    Co::InputWordInfo word;
    word.notes.resize(3);
    word.notes[0].duration = 0.1;
    word.notes[1].duration = 0.2;
    word.notes[2].duration = 0.3;
    REQUIRE(approxEqual(getWordDuration(word), 0.6));
}

// ---------------------------------------------------------------------------
// preprocessPhonemeTokens
// ---------------------------------------------------------------------------

TEST_CASE("preprocessPhonemeTokens empty words", "[inputword][tokens]") {
    std::vector<Co::InputWordInfo> words;
    std::map<std::string, int> tokens{{"a", 0}, {"b", 1}};
    auto exp = preprocessPhonemeTokens(words, tokens);
    // Empty input produces a zero-element tensor — may succeed or fail.
    // Just verify no crash.
    if (exp) {
        auto tensor = exp.take();
        // tensor may or may not be null for zero elements
    }
}

TEST_CASE("preprocessPhonemeTokens simple match", "[inputword][tokens]") {
    std::vector<Co::InputWordInfo> words(1);
    words[0].phones.resize(2);
    words[0].phones[0].token = "a";
    words[0].phones[1].token = "b";

    std::map<std::string, int> tokens{{"a", 10}, {"b", 20}};
    auto exp = preprocessPhonemeTokens(words, tokens);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    REQUIRE(tensor);
    REQUIRE(tensor->elementCount() == 2);
    auto view = tensor->view<int64_t>();
    REQUIRE(view.size() == 2);
    REQUIRE(view[0] == 10);
    REQUIRE(view[1] == 20);
}

TEST_CASE("preprocessPhonemeTokens unknown token returns error", "[inputword][tokens]") {
    std::vector<Co::InputWordInfo> words(1);
    words[0].phones.resize(1);
    words[0].phones[0].token = "xyz";

    std::map<std::string, int> tokens{{"a", 0}};
    auto exp = preprocessPhonemeTokens(words, tokens);
    REQUIRE(!exp.hasValue());
}

TEST_CASE("preprocessPhonemeTokens SP token without language prefix", "[inputword][tokens]") {
    // SP and AP tokens should be looked up without language prefix even when
    // phone.language is set.
    std::vector<Co::InputWordInfo> words(1);
    words[0].phones.resize(2);
    words[0].phones[0].token = "SP";
    words[0].phones[0].language = "op"; // language set but SP should ignore it
    words[0].phones[1].token = "a";
    words[0].phones[1].language = "op";

    std::map<std::string, int> tokens{{"SP", 0}, {"op/a", 1}, {"a", 2}};
    auto exp = preprocessPhonemeTokens(words, tokens);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<int64_t>();
    REQUIRE(view.size() == 2);
    REQUIRE(view[0] == 0);  // SP matched directly
    REQUIRE(view[1] == 1);  // op/a matched with language prefix
}

TEST_CASE("preprocessPhonemeTokens AP token without language prefix", "[inputword][tokens]") {
    std::vector<Co::InputWordInfo> words(1);
    words[0].phones.resize(1);
    words[0].phones[0].token = "AP";
    words[0].phones[0].language = "en";

    std::map<std::string, int> tokens{{"AP", 5}};
    auto exp = preprocessPhonemeTokens(words, tokens);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<int64_t>();
    REQUIRE(view[0] == 5);
}

TEST_CASE("preprocessPhonemeTokens fallback without language prefix", "[inputword][tokens]") {
    // When lang/token is not found, fall back to token-only lookup.
    std::vector<Co::InputWordInfo> words(1);
    words[0].phones.resize(1);
    words[0].phones[0].token = "a";
    words[0].phones[0].language = "op";

    std::map<std::string, int> tokens{{"a", 7}}; // only token, no "op/a"
    auto exp = preprocessPhonemeTokens(words, tokens);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<int64_t>();
    REQUIRE(view[0] == 7);
}

TEST_CASE("preprocessPhonemeTokens empty language uses token directly", "[inputword][tokens]") {
    std::vector<Co::InputWordInfo> words(1);
    words[0].phones.resize(1);
    words[0].phones[0].token = "a";
    words[0].phones[0].language = ""; // empty language

    std::map<std::string, int> tokens{{"a", 3}};
    auto exp = preprocessPhonemeTokens(words, tokens);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<int64_t>();
    REQUIRE(view[0] == 3);
}

TEST_CASE("preprocessPhonemeTokens multiple words", "[inputword][tokens]") {
    std::vector<Co::InputWordInfo> words(2);
    words[0].phones.resize(2);
    words[0].phones[0].token = "a";
    words[0].phones[1].token = "b";
    words[1].phones.resize(1);
    words[1].phones[0].token = "c";

    std::map<std::string, int> tokens{{"a", 1}, {"b", 2}, {"c", 3}};
    auto exp = preprocessPhonemeTokens(words, tokens);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<int64_t>();
    REQUIRE(view.size() == 3);
    REQUIRE(view[0] == 1);
    REQUIRE(view[1] == 2);
    REQUIRE(view[2] == 3);
}

// ---------------------------------------------------------------------------
// preprocessPhonemeLanguages
// ---------------------------------------------------------------------------

TEST_CASE("preprocessPhonemeLanguages simple match", "[inputword][languages]") {
    std::vector<Co::InputWordInfo> words(1);
    words[0].phones.resize(2);
    words[0].phones[0].language = "op";
    words[0].phones[1].language = "en";

    std::map<std::string, int> languages{{"op", 0}, {"en", 1}};
    auto exp = preprocessPhonemeLanguages(words, languages);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<int64_t>();
    REQUIRE(view.size() == 2);
    REQUIRE(view[0] == 0);
    REQUIRE(view[1] == 1);
}

TEST_CASE("preprocessPhonemeLanguages unknown language returns error", "[inputword][languages]") {
    // Regression test for BF-14: error message should use phone.language, not phone.token
    std::vector<Co::InputWordInfo> words(1);
    words[0].phones.resize(1);
    words[0].phones[0].token = "a";
    words[0].phones[0].language = "xyz";

    std::map<std::string, int> languages{{"op", 0}};
    auto exp = preprocessPhonemeLanguages(words, languages);
    REQUIRE(!exp.hasValue());
    // Verify the error message mentions the language, not the token
    // (BF-14 fix: was "unknown language " + phone.token, now "unknown language " + phone.language)
    auto err = exp.takeError();
    // The error message should contain "xyz" (the language), not "a" (the token)
    REQUIRE(err.message().find("xyz") != std::string::npos);
}

TEST_CASE("preprocessPhonemeLanguages empty language matches empty key", "[inputword][languages]") {
    std::vector<Co::InputWordInfo> words(1);
    words[0].phones.resize(1);
    words[0].phones[0].language = "";

    std::map<std::string, int> languages{{"", 0}};
    auto exp = preprocessPhonemeLanguages(words, languages);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<int64_t>();
    REQUIRE(view[0] == 0);
}

TEST_CASE("preprocessPhonemeLanguages multiple words", "[inputword][languages]") {
    std::vector<Co::InputWordInfo> words(2);
    words[0].phones.resize(1);
    words[0].phones[0].language = "op";
    words[1].phones.resize(2);
    words[1].phones[0].language = "en";
    words[1].phones[1].language = "op";

    std::map<std::string, int> languages{{"op", 0}, {"en", 1}};
    auto exp = preprocessPhonemeLanguages(words, languages);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<int64_t>();
    REQUIRE(view.size() == 3);
    REQUIRE(view[0] == 0);
    REQUIRE(view[1] == 1);
    REQUIRE(view[2] == 0);
}

// ---------------------------------------------------------------------------
// preprocessPhonemeDurations
// ---------------------------------------------------------------------------

TEST_CASE("preprocessPhonemeDurations single phone single note", "[inputword][durations]") {
    std::vector<Co::InputWordInfo> words(1);
    words[0].phones.resize(1);
    words[0].phones[0].start = 0.0;
    words[0].notes.resize(1);
    words[0].notes[0].duration = 0.1; // 100ms

    double frameWidth = 0.01; // 10ms per frame
    int64_t targetLength = -1;
    auto exp = preprocessPhonemeDurations(words, frameWidth, &targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<int64_t>();
    REQUIRE(view.size() == 1);
    // 0.1 / 0.01 = 10 frames
    REQUIRE(view[0] == 10);
    REQUIRE(targetLength == 10);
}

TEST_CASE("preprocessPhonemeDurations two phones in one word", "[inputword][durations]") {
    std::vector<Co::InputWordInfo> words(1);
    words[0].phones.resize(2);
    words[0].phones[0].start = 0.0;
    words[0].phones[1].start = 0.05; // 50ms into the word
    words[0].notes.resize(1);
    words[0].notes[0].duration = 0.1; // 100ms total

    double frameWidth = 0.01; // 10ms per frame
    int64_t targetLength = -1;
    auto exp = preprocessPhonemeDurations(words, frameWidth, &targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<int64_t>();
    REQUIRE(view.size() == 2);
    // phone 0: [0, 0.05) -> 5 frames
    REQUIRE(view[0] == 5);
    // phone 1: [0.05, 0.1) -> 5 frames
    REQUIRE(view[1] == 5);
    REQUIRE(targetLength == 10);
}

TEST_CASE("preprocessPhonemeDurations multiple words", "[inputword][durations]") {
    std::vector<Co::InputWordInfo> words(2);
    // Word 1: 100ms, one phone at start=0
    words[0].phones.resize(1);
    words[0].phones[0].start = 0.0;
    words[0].notes.resize(1);
    words[0].notes[0].duration = 0.1;
    // Word 2: 200ms, one phone at start=0
    words[1].phones.resize(1);
    words[1].phones[0].start = 0.0;
    words[1].notes.resize(1);
    words[1].notes[0].duration = 0.2;

    double frameWidth = 0.01;
    int64_t targetLength = -1;
    auto exp = preprocessPhonemeDurations(words, frameWidth, &targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<int64_t>();
    REQUIRE(view.size() == 2);
    REQUIRE(view[0] == 10); // 0.1 / 0.01
    REQUIRE(view[1] == 20); // 0.2 / 0.01
    REQUIRE(targetLength == 30);
}

TEST_CASE("preprocessPhonemeDurations outTargetLength null is safe", "[inputword][durations]") {
    std::vector<Co::InputWordInfo> words(1);
    words[0].phones.resize(1);
    words[0].phones[0].start = 0.0;
    words[0].notes.resize(1);
    words[0].notes[0].duration = 0.1;

    double frameWidth = 0.01;
    // Pass nullptr for outTargetLength — should not crash
    auto exp = preprocessPhonemeDurations(words, frameWidth, nullptr);
    REQUIRE(exp.hasValue());
}

TEST_CASE("preprocessPhonemeDurations rounding consistency", "[inputword][durations]") {
    // Verify that frame counts round consistently and sum to the total.
    std::vector<Co::InputWordInfo> words(1);
    words[0].phones.resize(3);
    words[0].phones[0].start = 0.0;
    words[0].phones[1].start = 0.033;
    words[0].phones[2].start = 0.067;
    words[0].notes.resize(1);
    words[0].notes[0].duration = 0.1;

    double frameWidth = 0.01;
    int64_t targetLength = -1;
    auto exp = preprocessPhonemeDurations(words, frameWidth, &targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<int64_t>();
    REQUIRE(view.size() == 3);
    // Sum of individual frame counts should equal targetLength
    int64_t sum = 0;
    for (size_t i = 0; i < view.size(); ++i) {
        sum += view[i];
    }
    REQUIRE(sum == targetLength);
}

TEST_CASE("preprocessPhonemeDurations last phone crosses word boundary", "[inputword][durations]") {
    // When a phone is the last in its word and there's a next word,
    // the next phone start includes the next word's first phone start.
    std::vector<Co::InputWordInfo> words(2);
    // Word 1: 100ms, one phone at start=0
    words[0].phones.resize(1);
    words[0].phones[0].start = 0.0;
    words[0].notes.resize(1);
    words[0].notes[0].duration = 0.1;
    // Word 2: 100ms, one phone at start=0.02 (20ms into word)
    words[1].phones.resize(1);
    words[1].phones[0].start = 0.02;
    words[1].notes.resize(1);
    words[1].notes[0].duration = 0.1;

    double frameWidth = 0.01;
    int64_t targetLength = -1;
    auto exp = preprocessPhonemeDurations(words, frameWidth, &targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<int64_t>();
    REQUIRE(view.size() == 2);
    // Phone 0 (word 0): start=0, next=0.1+0.02=0.12 -> 12 frames
    REQUIRE(view[0] == 12);
    // Phone 1 (word 1): start=0.1+0.02=0.12, next=0.1+0.1=0.2 -> 8 frames
    REQUIRE(view[1] == 8);
    REQUIRE(targetLength == 20);
}

TEST_CASE("preprocessPhonemeDurations next word with empty phones", "[inputword][durations]") {
    // When the next word has empty phones, the current word's last phone
    // duration should not include any extra offset.
    std::vector<Co::InputWordInfo> words(2);
    words[0].phones.resize(1);
    words[0].phones[0].start = 0.0;
    words[0].notes.resize(1);
    words[0].notes[0].duration = 0.1;
    // Word 2 has no phones
    words[1].phones.resize(0);
    words[1].notes.resize(1);
    words[1].notes[0].duration = 0.05;

    double frameWidth = 0.01;
    auto exp = preprocessPhonemeDurations(words, frameWidth, nullptr);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<int64_t>();
    REQUIRE(view.size() == 1);
    // With next word having empty phones, nextPhoneStart = phoneDurSum + wordDuration
    // = 0 + 0.1 = 0.1 -> 10 frames
    REQUIRE(view[0] == 10);
}
