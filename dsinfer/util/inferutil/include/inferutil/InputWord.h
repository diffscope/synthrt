#ifndef DSINFER_INFERUTIL_INPUTWORD_H
#define DSINFER_INFERUTIL_INPUTWORD_H

#include <cstddef>
#include <map>
#include <vector>

#include <synthrt/Support/Expected.h>

#include <dsinfer/Core/Tensor.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>


namespace ds::inferutil {

    inline size_t getPhoneCount(const std::vector<Api::Common::L1::InputWordInfo> &words) {
        size_t phoneCount = 0;
        for (const auto &word : words) {
            phoneCount += word.phones.size();
        }
        return phoneCount;
    }

    inline size_t getNoteCount(const std::vector<Api::Common::L1::InputWordInfo> &words) {
        size_t noteCount = 0;
        for (const auto &word : words) {
            noteCount += word.notes.size();
        }
        return noteCount;
    }

    inline double getWordDuration(const Api::Common::L1::InputWordInfo &word) {
        double wordDuration = 0;
        for (const auto &note : word.notes) {
            wordDuration += note.duration;
        }
        return wordDuration;
    }

    srt::Expected<srt::NO<ITensor>>
        preprocessPhonemeTokens(const std::vector<Api::Common::L1::InputWordInfo> &words,
                                const std::map<std::string, int> &tokens);

    srt::Expected<srt::NO<ITensor>>
        preprocessPhonemeLanguages(const std::vector<Api::Common::L1::InputWordInfo> &words,
                                   const std::map<std::string, int> &languages);

    srt::Expected<srt::NO<ITensor>>
        preprocessPhonemeDurations(const std::vector<Api::Common::L1::InputWordInfo> &words,
                                   double frameWidth, int64_t *outTargetLength = nullptr);
}
#endif // DSINFER_INFERUTIL_INPUTWORD_H