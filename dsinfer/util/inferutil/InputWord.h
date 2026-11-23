#ifndef DSINFER_INFERUTIL_INPUTWORD_H
#define DSINFER_INFERUTIL_INPUTWORD_H

#include <cstddef>
#include <map>
#include <vector>

#include <synthrt/Support/Expected.h>

#include <dsinfer/Core/Tensor.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>


namespace ds::inferutil {

    srt::Expected<std::shared_ptr<ITensor>>
        preprocessPhonemeTokens(const std::vector<Api::Common::L1::InputWordInfo> &words,
                                const std::map<std::string, int> &tokens);

    srt::Expected<std::shared_ptr<ITensor>>
        preprocessPhonemeLanguages(const std::vector<Api::Common::L1::InputWordInfo> &words,
                                   const std::map<std::string, int> &languages);

    srt::Expected<std::shared_ptr<ITensor>>
        preprocessPhonemeDurations(const std::vector<Api::Common::L1::InputWordInfo> &words,
                                   double frameWidth, int64_t *outTargetLength = nullptr);

}
#endif // DSINFER_INFERUTIL_INPUTWORD_H
