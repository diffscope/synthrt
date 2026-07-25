#pragma once

#include <map>
#include <vector>

#include <synthrt/Core/Support/Expected.h>

#include <synthrt/Driver/onnx/OnnxDriverApi.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace ds::infer::inferutil {
    srt::core::Expected<srt::core::NO<srt::driver::onnx::SessionStartInput>>
        preprocessLinguisticPhoneme(const std::vector<srt::svs::Api::Common::L1::InputWordInfo> &words,
                                    const std::map<std::string, int> &tokens,
                                    const std::map<std::string, int> &languages, bool useLanguageId,
                                    double frameWidth);

    srt::core::Expected<srt::core::NO<srt::driver::onnx::SessionStartInput>>
        preprocessLinguisticWord(const std::vector<srt::svs::Api::Common::L1::InputWordInfo> &words,
                                 const std::map<std::string, int> &tokens,
                                 const std::map<std::string, int> &languages, bool useLanguageId,
                                 double frameWidth);

    srt::core::Expected<void> runEncoder(const srt::core::NO<srt::driver::InferenceSession> &encoderSession,
                                   const srt::core::NO<srt::core::TaskStartInput> &linguisticInput,
                                   srt::core::NO<srt::driver::onnx::SessionStartInput> &out,
                                   bool useXMasks = true);
}