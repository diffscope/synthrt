#ifndef DSINFER_INFERUTIL_INPUTWORD_H
#define DSINFER_INFERUTIL_INPUTWORD_H

#include <cstddef>
#include <map>
#include <vector>

#include <synthrt/Core/Support/Expected.h>

#include <synthrt/Core/Tensor/ITensor.h>
#include <synthrt/Core/Tensor/Tensor.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>


namespace ds::infer::inferutil {

    inline size_t getPhoneCount(const std::vector<srt::svs::Api::Common::L1::InputWordInfo> &words) {
        size_t phoneCount = 0;
        for (const auto &word : words) {
            phoneCount += word.phones.size();
        }
        return phoneCount;
    }

    inline size_t getNoteCount(const std::vector<srt::svs::Api::Common::L1::InputWordInfo> &words) {
        size_t noteCount = 0;
        for (const auto &word : words) {
            noteCount += word.notes.size();
        }
        return noteCount;
    }

    inline double getWordDuration(const srt::svs::Api::Common::L1::InputWordInfo &word) {
        double wordDuration = 0;
        for (const auto &note : word.notes) {
            wordDuration += note.duration;
        }
        return wordDuration;
    }

    srt::core::Expected<srt::core::NO<srt::core::ITensor>>
        preprocessPhonemeTokens(const std::vector<srt::svs::Api::Common::L1::InputWordInfo> &words,
                                const std::map<std::string, int> &tokens);

    srt::core::Expected<srt::core::NO<srt::core::ITensor>>
        preprocessPhonemeLanguages(const std::vector<srt::svs::Api::Common::L1::InputWordInfo> &words,
                                   const std::map<std::string, int> &languages);

    srt::core::Expected<srt::core::NO<srt::core::ITensor>>
        preprocessPhonemeDurations(const std::vector<srt::svs::Api::Common::L1::InputWordInfo> &words,
                                   double frameWidth, int64_t *outTargetLength = nullptr);
}
#endif // DSINFER_INFERUTIL_INPUTWORD_H