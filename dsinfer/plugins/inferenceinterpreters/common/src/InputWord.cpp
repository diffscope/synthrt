#include <InterpreterCommon/InputWord.h>

#include <cmath>
#include <cstdint>

#include <stdcorelib/stdc_global.h>

#include <InterpreterCommon/TensorHelper.h>

namespace ds::InterpreterCommon {
    namespace Co = Api::Common::L1;

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

}