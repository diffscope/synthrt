#ifndef DSINFER_INTERPRETER_COMMON_LINGUISTICENCODER_H
#define DSINFER_INTERPRETER_COMMON_LINGUISTICENCODER_H

#include <map>
#include <vector>

#include <synthrt/Support/Expected.h>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace ds::InterpreterCommon {
    srt::Expected<srt::NO<Api::Onnx::SessionStartInput>>
        preprocessLinguisticPhoneme(const std::vector<Api::Common::L1::InputWordInfo> &words,
                                    const std::map<std::string, int> &tokens,
                                    const std::map<std::string, int> &languages, bool useLanguageId,
                                    double frameWidth);

    srt::Expected<srt::NO<Api::Onnx::SessionStartInput>>
        preprocessLinguisticWord(const std::vector<Api::Common::L1::InputWordInfo> &words,
                                 const std::map<std::string, int> &tokens,
                                 const std::map<std::string, int> &languages, bool useLanguageId,
                                 double frameWidth);

    srt::Expected<void> runEncoder(const srt::NO<InferenceSession> &encoderSession,
                                   const srt::NO<srt::TaskStartInput> &linguisticInput,
                                   srt::NO<Api::Onnx::SessionStartInput> &out);
}
#endif // DSINFER_INTERPRETER_COMMON_LINGUISTICENCODER_H