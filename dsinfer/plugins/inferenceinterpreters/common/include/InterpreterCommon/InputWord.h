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
        parsePhonemeTokens(const std::vector<Api::Common::L1::InputWordInfo> &words,
                           const std::map<std::string, int> &name2token);

    srt::Expected<srt::NO<ITensor>>
        parsePhonemeLanguages(const std::vector<Api::Common::L1::InputWordInfo> &words,
                              const std::map<std::string, int> &languages);

    srt::Expected<srt::NO<ITensor>>
        parsePhonemeDurations(const std::vector<Api::Common::L1::InputWordInfo> &words,
                              double frameLength, int64_t *outTargetLength = nullptr);
}
#endif // DSINFER_INTERPRETER_COMMON_INPUTWORD_H