#ifndef DSINFER_INPUTPARSERCOMMON_P_H
#define DSINFER_INPUTPARSERCOMMON_P_H

#include <synthrt/Support/Expected.h>
#include <synthrt/Support/JSON.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace ds {

    srt::Expected<void> parseValueCurve(const srt::JsonObject &parameter,
                                        const std::string &paramName, double &outInterval,
                                        std::vector<double> &outValues);

    srt::Expected<void> parseWords(const srt::JsonObject &obj,
                                   std::vector<Api::Common::L1::InputWordInfo> &outWords);
    srt::Expected<void>
        parseParameters(const srt::JsonObject &obj, bool pitchOnly,
                        std::vector<Api::Common::L1::InputParameterInfo> &outParameters);

    srt::Expected<void> parseSpeakers(const srt::JsonObject &obj,
                                      std::vector<Api::Common::L1::InputSpeakerInfo> &outSpeakers);
}

#endif // DSINFER_INPUTPARSERCOMMON_P_H