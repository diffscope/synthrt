#ifndef DSINFER_INTERPRETER_COMMON_INPUTWORD_H
#define DSINFER_INTERPRETER_COMMON_INPUTWORD_H

#include <cstddef>
#include <map>
#include <vector>

#include <synthrt/Support/Expected.h>

#include <dsinfer/Core/Tensor.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>


namespace ds::InterpreterCommon {
    size_t getPhoneCount(const std::vector<Api::Common::L1::InputWordInfo> &words);

    double getWordDuration(const Api::Common::L1::InputWordInfo &word);

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
#endif // DSINFER_INTERPRETER_COMMON_INPUTWORD_H