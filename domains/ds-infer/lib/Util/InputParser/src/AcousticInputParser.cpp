#include "AcousticInputParser.h"

#include "InputParserCommon_p.h"

namespace ds::infer {
    namespace Ac = srt::svs::Api::Acoustic::L1;

    srt::core::Expected<srt::core::NO<Ac::AcousticStartInput>>
        parseAcousticStartInput(const srt::core::JsonObject &obj) {

        auto input = srt::core::NO<Ac::AcousticStartInput>::create();

        if (auto it_duration = obj.find("duration"); it_duration != obj.end()) {
            input->duration = it_duration->second.toDouble();
        }

        if (auto it_steps = obj.find("steps"); it_steps != obj.end()) {
            if (!it_steps->second.isNumber()) {
                return srt::core::Error(srt::core::Error::InvalidFormat, "steps must be a number");
            }
            input->steps = it_steps->second.toInt();
        }

        if (auto it_depth = obj.find("depth"); it_depth != obj.end()) {
            if (!it_depth->second.isNumber()) {
                return srt::core::Error(srt::core::Error::InvalidFormat, "depth must be a number");
            }
            input->depth = static_cast<float>(it_depth->second.toDouble());
        }

        if (auto exp = parseWords(obj, input->words); !exp) {
            return exp.takeError();
        }

        if (auto exp = parseParameters(obj, false, input->parameters); !exp) {
            return exp.takeError();
        }

        if (auto exp = parseSpeakers(obj, input->speakers); !exp) {
            return exp.takeError();
        }

        return input;
    }
}