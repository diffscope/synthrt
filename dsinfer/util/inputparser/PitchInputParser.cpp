#include "PitchInputParser.h"

#include "InputParserCommon_p.h"

namespace ds {

    namespace Pit = Api::Pitch::L1;

    srt::Expected<std::unique_ptr<Pit::PitchStartInput>>
        parsePitchStartInput(const srt::JsonObject &obj) {

        auto input = std::make_unique<Pit::PitchStartInput>();

        if (auto result = parseOptionalNumber(obj, "duration", input->duration); !result) {
            return result.takeError();
        }

        if (auto result = parseOptionalInteger(obj, "steps", input->steps); !result) {
            return result.takeError();
        }

        if (auto result = parseWords(obj, input->words); !result) {
            return result.takeError();
        }

        if (auto result = parseParameters(obj, true, input->parameters); !result) {
            return result.takeError();
        }

        if (auto result = parseSpeakers(obj, input->speakers); !result) {
            return result.takeError();
        }

        return input;
    }

}
