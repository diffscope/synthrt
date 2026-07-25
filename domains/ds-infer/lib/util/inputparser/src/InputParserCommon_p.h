#pragma once

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/JSON.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace ds::infer {

    srt::core::Expected<void> parseValueCurve(const srt::core::JsonObject &parameter,
                                        const std::string &paramName, double &outInterval,
                                        std::vector<double> &outValues);

    srt::core::Expected<void> parseWords(const srt::core::JsonObject &obj,
                                   std::vector<srt::svs::Api::Common::L1::InputWordInfo> &outWords);
    srt::core::Expected<void>
        parseParameters(const srt::core::JsonObject &obj, bool pitchOnly,
                        std::vector<srt::svs::Api::Common::L1::InputParameterInfo> &outParameters);

    srt::core::Expected<void> parseSpeakers(const srt::core::JsonObject &obj,
                                      std::vector<srt::svs::Api::Common::L1::InputSpeakerInfo> &outSpeakers);
}
