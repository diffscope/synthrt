#include "AcousticInputParser.h"

#include "InputParserCommon_p.h"

namespace ds {

    namespace Ac = Api::Acoustic::L1;

    srt::Expected<std::unique_ptr<Ac::AcousticStartInput>>
        parseAcousticStartInput(const srt::JsonObject &obj) {

        auto input = std::make_unique<Ac::AcousticStartInput>();

        if (auto result = parseOptionalNumber(obj, "duration", input->duration); !result) {
            return result.takeError();
        }

        if (auto result = parseOptionalInteger(obj, "steps", input->steps); !result) {
            return result.takeError();
        }

        double depth = input->depth;
        if (auto result = parseOptionalNumber(obj, "depth", depth); !result) {
            return result.takeError();
        }
        input->depth = static_cast<float>(depth);

        if (auto result = parseWords(obj, input->words); !result) {
            return result.takeError();
        }

        if (auto result = parseParameters(obj, false, input->parameters); !result) {
            return result.takeError();
        }

        if (auto result = parseSpeakers(obj, input->speakers); !result) {
            return result.takeError();
        }

        return input;
    }

}
