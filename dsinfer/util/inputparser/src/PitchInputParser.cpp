#include "PitchInputParser.h"

#include "InputParserCommon_p.h"

namespace ds {
    namespace Pit = Api::Pitch::L1;

    srt::Expected<srt::NO<Pit::PitchStartInput>>
        parsePitchStartInput(const srt::JsonObject &obj) {

        auto input = srt::NO<Pit::PitchStartInput>::create();

        if (auto it_duration = obj.find("duration"); it_duration != obj.end()) {
            input->duration = it_duration->second.toDouble();
        }

        if (auto it_steps = obj.find("steps"); it_steps != obj.end()) {
            if (!it_steps->second.isNumber()) {
                return srt::Error(srt::Error::InvalidFormat, "steps must be a number");
            }
            input->steps = it_steps->second.toInt();
        }

        if (auto exp = parseWords(obj, input->words); !exp) {
            return exp.takeError();
        }

        if (auto exp = parseParameters(obj, true, input->parameters); !exp) {
            return exp.takeError();
        }

        if (auto exp = parseSpeakers(obj, input->speakers); !exp) {
            return exp.takeError();
        }

        return input;
    }
}