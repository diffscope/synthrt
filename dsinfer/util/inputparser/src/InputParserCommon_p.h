#ifndef DSINFER_INPUTPARSERCOMMON_P_H
#define DSINFER_INPUTPARSERCOMMON_P_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <synthrt/Support/Expected.h>
#include <synthrt/Support/JSON.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace ds {

    /// Parses an optional numeric field into a out.
    srt::Expected<void> parseOptionalNumber(const srt::JsonObject &obj, std::string_view fieldName,
                                            double &out);

    /// Parses an optional integer field into a out.
    srt::Expected<void> parseOptionalInteger(const srt::JsonObject &obj, std::string_view fieldName,
                                             int64_t &out);

    /// Parses one constant or sampled value curve.
    srt::Expected<void> parseValueCurve(const srt::JsonObject &parameter,
                                        const std::string &paramName, double &outInterval,
                                        std::vector<double> &outValues);

    /// Parses the score words shared by the inference input contracts.
    srt::Expected<void> parseWords(const srt::JsonObject &obj,
                                   std::vector<Api::Common::L1::InputWordInfo> &outWords);

    /// Parses parameter curves and optionally keeps only pitch related controls.
    srt::Expected<void>
        parseParameters(const srt::JsonObject &obj, bool pitchOnly,
                        std::vector<Api::Common::L1::InputParameterInfo> &outParameters);

    /// Parses top level speaker mixture curves.
    srt::Expected<void> parseSpeakers(const srt::JsonObject &obj,
                                      std::vector<Api::Common::L1::InputSpeakerInfo> &outSpeakers);

}

#endif // DSINFER_INPUTPARSERCOMMON_P_H
