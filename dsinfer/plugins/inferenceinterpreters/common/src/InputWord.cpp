#include <InterpreterCommon/InputWord.h>

#include <cmath>
#include <cstdint>

#include <stdcorelib/stdc_global.h>

#include <InterpreterCommon/TensorHelper.h>

namespace ds::InterpreterCommon {
    namespace Co = Api::Common::L1;

    static inline bool fillRestMidiWithNearestInPlace(std::vector<int64_t> &midi,
                                                      const std::vector<uint8_t> &isRest) {

        if (midi.size() != isRest.size()) {
            return false;
        }

        const size_t n = midi.size();

        size_t start = 0;
        while (start < n) {
            // Skip non-rest elements
            while (start < n && !isRest[start]) {
                ++start;
            }

            if (start >= n)
                break;

            size_t end = start;
            // Find contiguous rest region
            while (end < n && isRest[end]) {
                ++end;
            }

            // Handle [start, end)
            if (start > 0 && end < n) {
                // Middle segment
                int64_t left_val = midi[start - 1];
                int64_t right_val = midi[end];

                size_t mid = start + (end - start + 1) / 2; // split evenly

                for (size_t i = start; i < mid; ++i) {
                    midi[i] = left_val;
                }
                for (size_t i = mid; i < end; ++i) {
                    midi[i] = right_val;
                }
            } else if (start > 0) {
                // End segment
                int64_t fill_val = midi[start - 1];
                for (size_t i = start; i < end; ++i) {
                    midi[i] = fill_val;
                }
            } else if (end < n) {
                // Start segment
                int64_t fill_val = midi[end];
                for (size_t i = start; i < end; ++i) {
                    midi[i] = fill_val;
                }
            }

            start = end;
        }

        return true;
    }

    size_t getPhoneCount(const std::vector<Co::InputWordInfo> &words) {
        size_t phoneCount = 0;
        for (const auto &word : words) {
            phoneCount += word.phones.size();
        }
        return phoneCount;
    }

    double getWordDuration(const Co::InputWordInfo &word) {
        double wordDuration = 0;
        for (const auto &note : word.notes) {
            wordDuration += note.duration;
        }
        return wordDuration;
    }

    srt::Expected<srt::NO<ITensor>>
        preprocessPhonemeTokens(const std::vector<Co::InputWordInfo> &words,
                                const std::map<std::string, int> &tokens) {

        constexpr const char *SP_TOKEN = "SP";
        constexpr const char *AP_TOKEN = "AP";

        using TensorType = int64_t;
        auto phoneCount = getPhoneCount(words);
        auto exp = TensorHelper<TensorType>::createFor1DArray(phoneCount);
        if (!exp) {
            return exp.takeError();
        }
        auto &helper = exp.value();

        for (const auto &word : words) {
            for (const auto &phone : word.phones) {
                // tokens
                std::string tokenWithLang =
                    (phone.language.empty() || phone.token == SP_TOKEN || phone.token == AP_TOKEN)
                        ? phone.token
                        : (phone.language + '/' + phone.token);

                if (const auto it1 = tokens.find(tokenWithLang); it1 != tokens.end()) {
                    // first try finding the phoneme with the language tag (lang/phoneme)
                    helper.write(it1->second);
                } else if (const auto it2 = tokens.find(phone.token); it2 != tokens.end()) {
                    // then try finding the phoneme without the language tag (phoneme)
                    helper.write(it2->second);
                } else {
                    return srt::Error(srt::Error::InvalidArgument, "unknown token " + phone.token);
                }
            }
        }

        if (STDCORELIB_UNLIKELY(!helper.isComplete())) {
            return srt::Error(
                srt::Error::SessionError,
                "parsePhonemeTokens: tensor element count does not match phoneme count");
        }
        return helper.take();
    }

    srt::Expected<srt::NO<ITensor>>
        preprocessPhonemeLanguages(const std::vector<Co::InputWordInfo> &words,
                                   const std::map<std::string, int> &languages) {

        auto phoneCount = getPhoneCount(words);
        using TensorType = int64_t;
        auto exp = TensorHelper<TensorType>::createFor1DArray(phoneCount);
        if (!exp) {
            return exp.takeError();
        }
        auto &helper = exp.value();

        for (const auto &word : words) {
            for (const auto &phone : word.phones) {
                if (const auto it = languages.find(phone.language); it != languages.end()) {
                    helper.write(it->second);
                } else {
                    return srt::Error(srt::Error::InvalidArgument,
                                      "unknown language " + phone.token);
                }
            }
        }

        if (STDCORELIB_UNLIKELY(!helper.isComplete())) {
            return srt::Error(
                srt::Error::SessionError,
                "parsePhonemeLanguages: tensor element count does not match phoneme count");
        }

        return helper.take();
    }

    srt::Expected<srt::NO<ITensor>>
        preprocessPhonemeDurations(const std::vector<Co::InputWordInfo> &words, double frameWidth,
                                   int64_t *outTargetLength) {

        auto phoneCount = getPhoneCount(words);
        using TensorType = int64_t;
        auto exp = TensorHelper<TensorType>::createFor1DArray(phoneCount);
        if (!exp) {
            return exp.takeError();
        }
        auto &helper = exp.value();

        double phoneDurSum = 0.0;
        int64_t targetLength = 0;

        for (size_t currWordIndex = 0; currWordIndex < words.size(); ++currWordIndex) {
            const auto &word = words[currWordIndex];
            auto wordDuration = getWordDuration(word);

            for (size_t i = 0; i < word.phones.size(); ++i) {

                // durations
                {
                    bool currPhoneIsTheLastPhone = (i == word.phones.size() - 1);
                    auto currPhoneStart = phoneDurSum + word.phones[i].start;
                    auto nextPhoneStart =
                        phoneDurSum +
                        (currPhoneIsTheLastPhone ? wordDuration : word.phones[i + 1].start);
                    if (currPhoneIsTheLastPhone && (currWordIndex + 1 < words.size())) {
                        // If current word is not the last word
                        const auto &nextWord = words[currWordIndex + 1];
                        if (!nextWord.phones.empty()) {
                            nextPhoneStart += nextWord.phones[0].start;
                        }
                    }
                    int64_t currPhoneStartFrames = std::llround(currPhoneStart / frameWidth);
                    int64_t nextPhoneStartFrames = std::llround(nextPhoneStart / frameWidth);
                    int64_t currPhoneFrames = nextPhoneStartFrames - currPhoneStartFrames;
                    helper.write(currPhoneFrames);
                    targetLength += currPhoneFrames;
                }
            }
            phoneDurSum += wordDuration;
        }

        if (STDCORELIB_UNLIKELY(!helper.isComplete())) {
            return srt::Error(
                srt::Error::SessionError,
                "parsePhonemeDurations: tensor element count does not match phoneme count");
        }

        if (outTargetLength) {
            *outTargetLength = targetLength;
        }
        return helper.take();
    }

    srt::Expected<srt::NO<ITensor>>
        preprocessPhonemeMidi(const std::vector<Api::Common::L1::InputWordInfo> &words) {

        auto phoneCount = getPhoneCount(words);

        std::vector<uint8_t> isRest;
        std::vector<int64_t> phMidi;
        isRest.reserve(phoneCount);
        phMidi.reserve(phoneCount);

        for (const auto &word : words) {
            if (word.notes.empty())
                continue;

            std::vector<double> cumDur;
            double s = 0;
            for (const auto &note : word.notes) {
                s += note.duration;
                cumDur.push_back(s);
            }

            for (const auto &phone : word.phones) {
                size_t idx = 0;
                while (idx < cumDur.size() && phone.start > cumDur[idx]) {
                    ++idx;
                }
                if (idx >= word.notes.size())
                    idx = word.notes.size() - 1;

                const auto &note = word.notes[idx];
                const auto rest = static_cast<uint8_t>(note.is_rest);
                isRest.push_back(rest);
                phMidi.push_back(rest ? 0 : note.key);
            }

            if (!fillRestMidiWithNearestInPlace(phMidi, isRest)) {
                return srt::Error(srt::Error::SessionError, "failed to fill rest notes");
            }
        }

        std::vector<int64_t> shape{1, static_cast<int64_t>(phMidi.size())};
        if (auto exp = Tensor::createFromView<int64_t>(shape, stdc::array_view<int64_t>{phMidi});
            exp) {
            return exp.take();
        } else {
            return exp.takeError();
        }
    }
}